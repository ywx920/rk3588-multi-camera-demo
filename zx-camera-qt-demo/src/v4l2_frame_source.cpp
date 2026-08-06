#include "v4l2_frame_source.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <system_error>

namespace {

std::system_error system_error(const char* operation)
{
    return std::system_error(errno, std::generic_category(), operation);
}

}  // namespace

V4L2FrameSource::~V4L2FrameSource() noexcept
{
    close();
}

int V4L2FrameSource::xioctl(int fd, unsigned long request, void* argument)
{
    int result;
    do {
        result = ioctl(fd, request, argument);
    } while (result < 0 && errno == EINTR);
    return result;
}

std::uint32_t V4L2FrameSource::parse_fourcc(const std::string& text)
{
    if (text.size() != 4U) {
        throw std::invalid_argument("pixel format must contain four characters");
    }
    return v4l2_fourcc(text[0], text[1], text[2], text[3]);
}

void V4L2FrameSource::open(const std::string& device,
                           int width,
                           int height,
                           const std::string& fourcc,
                           unsigned int buffer_count)
{
    close();
    fd_ = ::open(device.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) throw system_error("open V4L2 device");

    try {
        v4l2_capability capability{};
        if (xioctl(fd_, VIDIOC_QUERYCAP, &capability) < 0) {
            throw system_error("VIDIOC_QUERYCAP");
        }
        const std::uint32_t caps =
            (capability.capabilities & V4L2_CAP_DEVICE_CAPS) != 0U
                ? capability.device_caps
                : capability.capabilities;
        if ((caps & V4L2_CAP_STREAMING) == 0U) {
            throw std::runtime_error("V4L2 device does not support streaming");
        }
        if ((caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) != 0U) {
            multi_planar_ = true;
            type_ = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        } else if ((caps & V4L2_CAP_VIDEO_CAPTURE) != 0U) {
            multi_planar_ = false;
            type_ = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        } else {
            throw std::runtime_error("node is not a video capture device");
        }

        fourcc_ = parse_fourcc(fourcc);
        v4l2_format format{};
        format.type = type_;
        if (multi_planar_) {
            format.fmt.pix_mp.width = static_cast<__u32>(width);
            format.fmt.pix_mp.height = static_cast<__u32>(height);
            format.fmt.pix_mp.pixelformat = fourcc_;
            format.fmt.pix_mp.field = V4L2_FIELD_ANY;
        } else {
            format.fmt.pix.width = static_cast<__u32>(width);
            format.fmt.pix.height = static_cast<__u32>(height);
            format.fmt.pix.pixelformat = fourcc_;
            format.fmt.pix.field = V4L2_FIELD_ANY;
        }
        if (xioctl(fd_, VIDIOC_S_FMT, &format) < 0) {
            throw system_error("VIDIOC_S_FMT");
        }
        if (xioctl(fd_, VIDIOC_G_FMT, &format) < 0) {
            throw system_error("VIDIOC_G_FMT");
        }
        if (multi_planar_) {
            width_ = static_cast<int>(format.fmt.pix_mp.width);
            height_ = static_cast<int>(format.fmt.pix_mp.height);
            fourcc_ = format.fmt.pix_mp.pixelformat;
        } else {
            width_ = static_cast<int>(format.fmt.pix.width);
            height_ = static_cast<int>(format.fmt.pix.height);
            fourcc_ = format.fmt.pix.pixelformat;
        }

        v4l2_requestbuffers request{};
        request.count = buffer_count < 4U ? 4U : buffer_count;
        request.type = type_;
        request.memory = V4L2_MEMORY_MMAP;
        if (xioctl(fd_, VIDIOC_REQBUFS, &request) < 0) {
            throw system_error("VIDIOC_REQBUFS");
        }
        if (request.count < 2U) {
            throw std::runtime_error("driver returned fewer than two buffers");
        }

        buffers_.resize(request.count);
        for (unsigned int index = 0; index < request.count; ++index) {
            v4l2_buffer buffer{};
            v4l2_plane planes[VIDEO_MAX_PLANES]{};
            buffer.type = type_;
            buffer.memory = V4L2_MEMORY_MMAP;
            buffer.index = index;
            if (multi_planar_) {
                buffer.length = VIDEO_MAX_PLANES;
                buffer.m.planes = planes;
            }
            if (xioctl(fd_, VIDIOC_QUERYBUF, &buffer) < 0) {
                throw system_error("VIDIOC_QUERYBUF");
            }
            const unsigned int plane_count =
                multi_planar_ ? buffer.length : 1U;
            buffers_[index].planes.resize(plane_count);
            for (unsigned int plane = 0; plane < plane_count; ++plane) {
                const std::size_t length = multi_planar_
                    ? planes[plane].length : buffer.length;
                const off_t offset = static_cast<off_t>(
                    multi_planar_ ? planes[plane].m.mem_offset
                                  : buffer.m.offset);
                void* address = mmap(nullptr, length, PROT_READ | PROT_WRITE,
                                     MAP_SHARED, fd_, offset);
                if (address == MAP_FAILED) throw system_error("mmap");
                int dma_fd = -1;
                v4l2_exportbuffer export_buffer{};
                export_buffer.type = type_;
                export_buffer.index = index;
                export_buffer.plane = plane;
                export_buffer.flags = O_CLOEXEC;
                if (xioctl(fd_, VIDIOC_EXPBUF, &export_buffer) == 0)
                    dma_fd = export_buffer.fd;
                buffers_[index].planes[plane] = {address, length, dma_fd};
            }
        }
        for (unsigned int index = 0; index < request.count; ++index) {
            queue_buffer(index);
        }
        if (xioctl(fd_, VIDIOC_STREAMON, &type_) < 0) {
            throw system_error("VIDIOC_STREAMON");
        }
        streaming_ = true;
    } catch (...) {
        close();
        throw;
    }
}

void V4L2FrameSource::queue_buffer(unsigned int index)
{
    v4l2_buffer buffer{};
    v4l2_plane planes[VIDEO_MAX_PLANES]{};
    buffer.type = type_;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.index = index;
    if (multi_planar_) {
        buffer.length =
            static_cast<__u32>(buffers_.at(index).planes.size());
        buffer.m.planes = planes;
    }
    if (xioctl(fd_, VIDIOC_QBUF, &buffer) < 0) {
        throw system_error("VIDIOC_QBUF");
    }
}

bool V4L2FrameSource::capture_one(Frame& frame, int timeout_ms,
                                  bool copy_data, bool hold_buffer)
{
    pollfd descriptor{fd_, POLLIN | POLLPRI, 0};
    int ready;
    do {
        ready = poll(&descriptor, 1, timeout_ms);
    } while (ready < 0 && errno == EINTR);
    if (ready < 0) throw system_error("poll");
    if (ready == 0) return false;
    if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        throw std::runtime_error("V4L2 poll reported device error");
    }

    v4l2_buffer buffer{};
    v4l2_plane planes[VIDEO_MAX_PLANES]{};
    buffer.type = type_;
    buffer.memory = V4L2_MEMORY_MMAP;
    if (multi_planar_) {
        buffer.length = VIDEO_MAX_PLANES;
        buffer.m.planes = planes;
    }
    if (xioctl(fd_, VIDIOC_DQBUF, &buffer) < 0) {
        if (errno == EAGAIN) return false;
        throw system_error("VIDIOC_DQBUF");
    }
    if (buffer.index >= buffers_.size()) {
        throw std::runtime_error("driver returned invalid buffer index");
    }

    try {
        frame.data.clear();
        frame.address = nullptr;
        frame.bytes_used = 0;
        frame.dma_fd = -1;
        frame.held = false;
        frame.sequence = buffer.sequence;
        frame.timestamp = buffer.timestamp;
        if (!copy_data && !hold_buffer) {
            queue_buffer(buffer.index);
            return true;
        }
        const unsigned int plane_count =
            multi_planar_ ? buffer.length : 1U;
        if (plane_count > 0U) {
            const auto& first = buffers_[buffer.index].planes.front();
            frame.address = static_cast<const std::uint8_t*>(first.address);
            frame.bytes_used = multi_planar_
                ? static_cast<std::size_t>(planes[0].bytesused)
                : static_cast<std::size_t>(buffer.bytesused);
            frame.dma_fd = first.dma_fd;
            frame.buffer_index = buffer.index;
        }
        for (unsigned int plane = 0; copy_data && plane < plane_count; ++plane) {
            const auto& mapping = buffers_[buffer.index].planes.at(plane);
            const std::size_t used = multi_planar_
                ? static_cast<std::size_t>(planes[plane].bytesused)
                : static_cast<std::size_t>(buffer.bytesused);
            if (used > mapping.length) {
                throw std::runtime_error("bytesused exceeds mapped buffer");
            }
            const auto* begin =
                static_cast<const std::uint8_t*>(mapping.address);
            frame.data.insert(frame.data.end(), begin, begin + used);
        }
    } catch (...) {
        queue_buffer(buffer.index);
        throw;
    }
    if (hold_buffer) frame.held = true;
    else queue_buffer(buffer.index);
    return true;
}

void V4L2FrameSource::release(Frame& frame)
{
    if (!frame.held) return;
    queue_buffer(frame.buffer_index);
    frame.held = false;
    frame.address = nullptr;
    frame.bytes_used = 0;
    frame.dma_fd = -1;
}

std::string V4L2FrameSource::format() const
{
    std::string value(4, '\0');
    value[0] = static_cast<char>(fourcc_ & 0xffU);
    value[1] = static_cast<char>((fourcc_ >> 8U) & 0xffU);
    value[2] = static_cast<char>((fourcc_ >> 16U) & 0xffU);
    value[3] = static_cast<char>((fourcc_ >> 24U) & 0xffU);
    return value;
}

void V4L2FrameSource::close() noexcept
{
    if (fd_ >= 0 && streaming_) {
        (void)xioctl(fd_, VIDIOC_STREAMOFF, &type_);
    }
    streaming_ = false;
    for (auto& buffer : buffers_) {
        for (auto& plane : buffer.planes) {
            if (plane.dma_fd >= 0) {
                (void)::close(plane.dma_fd);
                plane.dma_fd = -1;
            }
            if (plane.address != MAP_FAILED) {
                (void)munmap(plane.address, plane.length);
                plane.address = MAP_FAILED;
            }
        }
    }
    buffers_.clear();
    if (fd_ >= 0) {
        (void)::close(fd_);
        fd_ = -1;
    }
    width_ = 0;
    height_ = 0;
    fourcc_ = 0;
}
