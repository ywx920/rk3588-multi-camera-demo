#include "v4l2_mjpeg_camera.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <stdexcept>
#include <system_error>

namespace {
std::system_error error(const char* operation)
{
    return std::system_error(errno, std::generic_category(), operation);
}
}

V4L2Camera::~V4L2Camera() noexcept { close(); }

int V4L2Camera::xioctl(int fd, unsigned long request, void* argument)
{
    int result;
    do { result = ioctl(fd, request, argument); }
    while (result < 0 && errno == EINTR);
    return result;
}

std::uint32_t V4L2Camera::parse_fourcc(const std::string& text)
{
    if (text.size() != 4U) throw std::invalid_argument("FOURCC must be four characters");
    return v4l2_fourcc(text[0], text[1], text[2], text[3]);
}

void V4L2Camera::open(const std::string& device, int width, int height,
                      const std::string& fourcc, unsigned int buffer_count)
{
    close();
    fd_ = ::open(device.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) throw error("open camera");
    try {
        v4l2_capability capability{};
        if (xioctl(fd_, VIDIOC_QUERYCAP, &capability) < 0) throw error("VIDIOC_QUERYCAP");
        const std::uint32_t caps = capability.capabilities & V4L2_CAP_DEVICE_CAPS
            ? capability.device_caps : capability.capabilities;
        if (!(caps & V4L2_CAP_STREAMING)) throw std::runtime_error("device lacks streaming");
        if (caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
            multi_planar_ = true;
            type_ = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        } else if (caps & V4L2_CAP_VIDEO_CAPTURE) {
            multi_planar_ = false;
            type_ = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        } else throw std::runtime_error("not a capture device");

        fourcc_ = parse_fourcc(fourcc);
        v4l2_format format{};
        format.type = type_;
        if (multi_planar_) {
            format.fmt.pix_mp.width = width;
            format.fmt.pix_mp.height = height;
            format.fmt.pix_mp.pixelformat = fourcc_;
            format.fmt.pix_mp.field = V4L2_FIELD_ANY;
        } else {
            format.fmt.pix.width = width;
            format.fmt.pix.height = height;
            format.fmt.pix.pixelformat = fourcc_;
            format.fmt.pix.field = V4L2_FIELD_ANY;
        }
        if (xioctl(fd_, VIDIOC_S_FMT, &format) < 0) throw error("VIDIOC_S_FMT");
        if (multi_planar_) {
            width_ = format.fmt.pix_mp.width;
            height_ = format.fmt.pix_mp.height;
            fourcc_ = format.fmt.pix_mp.pixelformat;
        } else {
            width_ = format.fmt.pix.width;
            height_ = format.fmt.pix.height;
            fourcc_ = format.fmt.pix.pixelformat;
        }

        v4l2_requestbuffers request{};
        request.count = buffer_count < 4U ? 4U : buffer_count;
        request.type = type_;
        request.memory = V4L2_MEMORY_MMAP;
        if (xioctl(fd_, VIDIOC_REQBUFS, &request) < 0) throw error("VIDIOC_REQBUFS");
        if (request.count < 2U) throw std::runtime_error("too few V4L2 buffers");
        buffers_.resize(request.count);
        for (unsigned int index = 0; index < request.count; ++index) {
            v4l2_buffer buffer{};
            v4l2_plane planes[VIDEO_MAX_PLANES]{};
            buffer.type = type_;
            buffer.memory = V4L2_MEMORY_MMAP;
            buffer.index = index;
            if (multi_planar_) { buffer.length = VIDEO_MAX_PLANES; buffer.m.planes = planes; }
            if (xioctl(fd_, VIDIOC_QUERYBUF, &buffer) < 0) throw error("VIDIOC_QUERYBUF");
            const unsigned int count = multi_planar_ ? buffer.length : 1U;
            buffers_[index].planes.resize(count);
            for (unsigned int plane = 0; plane < count; ++plane) {
                const std::size_t length = multi_planar_ ? planes[plane].length : buffer.length;
                const off_t offset = multi_planar_ ? planes[plane].m.mem_offset : buffer.m.offset;
                void* address = mmap(nullptr, length, PROT_READ | PROT_WRITE,
                                     MAP_SHARED, fd_, offset);
                if (address == MAP_FAILED) throw error("mmap");
                buffers_[index].planes[plane] = {address, length};
            }
            queue(index);
        }
        if (xioctl(fd_, VIDIOC_STREAMON, &type_) < 0) throw error("VIDIOC_STREAMON");
        streaming_ = true;
    } catch (...) { close(); throw; }
}

void V4L2Camera::queue(unsigned int index)
{
    v4l2_buffer buffer{};
    v4l2_plane planes[VIDEO_MAX_PLANES]{};
    buffer.type = type_;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.index = index;
    if (multi_planar_) {
        buffer.length = static_cast<__u32>(buffers_.at(index).planes.size());
        buffer.m.planes = planes;
    }
    if (xioctl(fd_, VIDIOC_QBUF, &buffer) < 0) throw error("VIDIOC_QBUF");
}

bool V4L2Camera::capture(Frame& frame, int timeout_ms, bool copy_data)
{
    pollfd descriptor{fd_, POLLIN | POLLPRI, 0};
    int ready;
    do { ready = poll(&descriptor, 1, timeout_ms); } while (ready < 0 && errno == EINTR);
    if (ready < 0) throw error("poll");
    if (ready == 0) return false;
    if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))
        throw std::runtime_error("camera poll error");
    v4l2_buffer buffer{};
    v4l2_plane planes[VIDEO_MAX_PLANES]{};
    buffer.type = type_;
    buffer.memory = V4L2_MEMORY_MMAP;
    if (multi_planar_) { buffer.length = VIDEO_MAX_PLANES; buffer.m.planes = planes; }
    if (xioctl(fd_, VIDIOC_DQBUF, &buffer) < 0) {
        if (errno == EAGAIN) return false;
        throw error("VIDIOC_DQBUF");
    }
    try {
        frame.data.clear();
        frame.sequence = buffer.sequence;
        const unsigned int count = multi_planar_ ? buffer.length : 1U;
        for (unsigned int plane = 0; copy_data && plane < count; ++plane) {
            const auto& mapping = buffers_.at(buffer.index).planes.at(plane);
            const std::size_t used = multi_planar_ ? planes[plane].bytesused : buffer.bytesused;
            if (used > mapping.length) throw std::runtime_error("invalid bytesused");
            const auto* begin = static_cast<const std::uint8_t*>(mapping.address);
            frame.data.insert(frame.data.end(), begin, begin + used);
        }
    } catch (...) { queue(buffer.index); throw; }
    queue(buffer.index);
    return true;
}

std::string V4L2Camera::format() const
{
    std::string value(4, '\0');
    for (unsigned int i = 0; i < 4; ++i) value[i] = static_cast<char>((fourcc_ >> (8U * i)) & 0xffU);
    return value;
}

void V4L2Camera::close() noexcept
{
    if (fd_ >= 0 && streaming_) (void)xioctl(fd_, VIDIOC_STREAMOFF, &type_);
    streaming_ = false;
    for (auto& buffer : buffers_) for (auto& plane : buffer.planes)
        if (plane.address != MAP_FAILED) (void)munmap(plane.address, plane.length);
    buffers_.clear();
    if (fd_ >= 0) (void)::close(fd_);
    fd_ = -1;
}
