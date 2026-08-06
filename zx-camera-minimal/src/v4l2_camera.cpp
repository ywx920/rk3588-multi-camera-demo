#include "v4l2_camera.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace {

std::runtime_error system_error(const std::string& operation)
{
    return std::runtime_error(operation + ": " + std::strerror(errno));
}

std::string timeval_string(const timeval& value)
{
    std::ostringstream output;
    output << value.tv_sec << '.' << std::setfill('0') << std::setw(6)
           << value.tv_usec;
    return output.str();
}

}  // namespace

std::size_t V4L2Camera::FrameView::total_bytes() const noexcept
{
    std::size_t total = 0;
    for (const auto& plane : planes) {
        total += plane.bytes_used;
    }
    return total;
}

V4L2Camera::V4L2Camera(CameraConfig config) : config_(std::move(config)) {}

V4L2Camera::~V4L2Camera() noexcept
{
    cleanup();
}

int V4L2Camera::xioctl(int fd, unsigned long request, void* argument)
{
    int result;
    do {
        result = ::ioctl(fd, request, argument);
    } while (result == -1 && errno == EINTR);
    return result;
}

std::string V4L2Camera::fourcc_to_string(std::uint32_t fourcc)
{
    std::string result(4, ' ');
    result[0] = static_cast<char>(fourcc & 0xffU);
    result[1] = static_cast<char>((fourcc >> 8U) & 0xffU);
    result[2] = static_cast<char>((fourcc >> 16U) & 0xffU);
    result[3] = static_cast<char>((fourcc >> 24U) & 0xffU);
    return result;
}

std::uint32_t V4L2Camera::string_to_fourcc(const std::string& text)
{
    if (text.size() != 4) {
        throw std::runtime_error("FOURCC must contain exactly four characters");
    }
    return v4l2_fourcc(text[0], text[1], text[2], text[3]);
}

std::string V4L2Camera::capability_string(std::uint32_t caps)
{
    std::ostringstream text;
    text << "0x" << std::hex << std::setw(8) << std::setfill('0') << caps
         << std::dec;
    if ((caps & V4L2_CAP_VIDEO_CAPTURE) != 0U) text << " VIDEO_CAPTURE";
    if ((caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) != 0U)
        text << " VIDEO_CAPTURE_MPLANE";
    if ((caps & V4L2_CAP_STREAMING) != 0U) text << " STREAMING";
    return text.str();
}

void V4L2Camera::open_device()
{
    fd_ = ::open(config_.device.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) {
        throw system_error("open " + config_.device);
    }
}

void V4L2Camera::query_capabilities()
{
    v4l2_capability capability{};
    if (xioctl(fd_, VIDIOC_QUERYCAP, &capability) < 0) {
        throw system_error("VIDIOC_QUERYCAP");
    }

    capabilities_ = ((capability.capabilities & V4L2_CAP_DEVICE_CAPS) != 0U)
                        ? capability.device_caps
                        : capability.capabilities;

    if ((capabilities_ & V4L2_CAP_STREAMING) == 0U) {
        throw std::runtime_error("device does not support V4L2 streaming");
    }
    if ((capabilities_ & V4L2_CAP_VIDEO_CAPTURE_MPLANE) != 0U) {
        multi_planar_ = true;
        buffer_type_ = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    } else if ((capabilities_ & V4L2_CAP_VIDEO_CAPTURE) != 0U) {
        multi_planar_ = false;
        buffer_type_ = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    } else {
        throw std::runtime_error("device is not a video capture node");
    }

    std::cout << "Device             : " << config_.device << '\n'
              << "Driver             : " << capability.driver << '\n'
              << "Card               : " << capability.card << '\n'
              << "Bus information    : " << capability.bus_info << '\n'
              << "Capabilities       : " << capability_string(capabilities_) << '\n'
              << "Buffer type        : "
              << (multi_planar_ ? "VIDEO_CAPTURE_MPLANE" : "VIDEO_CAPTURE")
              << '\n';
}

void V4L2Camera::enumerate_formats() const
{
    std::cout << "Supported formats  :";
    bool found = false;
    for (std::uint32_t index = 0;; ++index) {
        v4l2_fmtdesc description{};
        description.index = index;
        description.type = buffer_type_;
        if (xioctl(fd_, VIDIOC_ENUM_FMT, &description) < 0) {
            if (errno == EINVAL) break;
            throw system_error("VIDIOC_ENUM_FMT");
        }
        found = true;
        std::cout << ' ' << fourcc_to_string(description.pixelformat);
    }
    std::cout << (found ? "\n" : " none\n");
}

void V4L2Camera::get_format(const char* label)
{
    v4l2_format format{};
    format.type = buffer_type_;
    if (xioctl(fd_, VIDIOC_G_FMT, &format) < 0) {
        throw system_error("VIDIOC_G_FMT");
    }

    bytes_per_line_.clear();
    size_image_.clear();
    if (multi_planar_) {
        actual_width_ = format.fmt.pix_mp.width;
        actual_height_ = format.fmt.pix_mp.height;
        actual_fourcc_ = format.fmt.pix_mp.pixelformat;
        const auto planes = format.fmt.pix_mp.num_planes;
        if (planes == 0 || planes > VIDEO_MAX_PLANES) {
            throw std::runtime_error("driver returned invalid plane count");
        }
        for (std::uint8_t i = 0; i < planes; ++i) {
            bytes_per_line_.push_back(
                format.fmt.pix_mp.plane_fmt[i].bytesperline);
            size_image_.push_back(format.fmt.pix_mp.plane_fmt[i].sizeimage);
        }
    } else {
        actual_width_ = format.fmt.pix.width;
        actual_height_ = format.fmt.pix.height;
        actual_fourcc_ = format.fmt.pix.pixelformat;
        bytes_per_line_.push_back(format.fmt.pix.bytesperline);
        size_image_.push_back(format.fmt.pix.sizeimage);
    }

    std::cout << label << " format       : " << fourcc_to_string(actual_fourcc_)
              << ' ' << actual_width_ << 'x' << actual_height_ << '\n';
}

void V4L2Camera::set_requested_format()
{
    v4l2_format format{};
    format.type = buffer_type_;
    const auto fourcc = string_to_fourcc(config_.format);

    if (multi_planar_) {
        format.fmt.pix_mp.width = config_.width;
        format.fmt.pix_mp.height = config_.height;
        format.fmt.pix_mp.pixelformat = fourcc;
        format.fmt.pix_mp.field = V4L2_FIELD_ANY;
    } else {
        format.fmt.pix.width = config_.width;
        format.fmt.pix.height = config_.height;
        format.fmt.pix.pixelformat = fourcc;
        format.fmt.pix.field = V4L2_FIELD_ANY;
    }

    std::cout << "Requested format   : " << config_.format << ' '
              << config_.width << 'x' << config_.height << '\n';
    if (xioctl(fd_, VIDIOC_S_FMT, &format) < 0) {
        throw system_error("VIDIOC_S_FMT");
    }
}

void V4L2Camera::request_and_map_buffers()
{
    v4l2_requestbuffers request{};
    request.count = config_.buffer_count;
    request.type = buffer_type_;
    request.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd_, VIDIOC_REQBUFS, &request) < 0) {
        throw system_error("VIDIOC_REQBUFS");
    }
    if (request.count < 4) {
        throw std::runtime_error("driver allocated fewer than 4 MMAP buffers");
    }

    buffers_.resize(request.count);
    try {
        for (std::uint32_t index = 0; index < request.count; ++index) {
            v4l2_buffer buffer{};
            std::array<v4l2_plane, VIDEO_MAX_PLANES> planes{};
            buffer.type = buffer_type_;
            buffer.memory = V4L2_MEMORY_MMAP;
            buffer.index = index;
            if (multi_planar_) {
                buffer.length = static_cast<std::uint32_t>(planes.size());
                buffer.m.planes = planes.data();
            }
            if (xioctl(fd_, VIDIOC_QUERYBUF, &buffer) < 0) {
                throw system_error("VIDIOC_QUERYBUF");
            }

            const std::uint32_t plane_count = multi_planar_ ? buffer.length : 1U;
            if (plane_count == 0 || plane_count > VIDEO_MAX_PLANES) {
                throw std::runtime_error("VIDIOC_QUERYBUF returned invalid planes");
            }
            buffers_[index].planes.resize(plane_count);
            for (std::uint32_t plane = 0; plane < plane_count; ++plane) {
                const std::size_t length =
                    multi_planar_ ? planes[plane].length : buffer.length;
                const off_t offset = static_cast<off_t>(
                    multi_planar_ ? planes[plane].m.mem_offset
                                  : buffer.m.offset);
                void* address = ::mmap(nullptr, length, PROT_READ | PROT_WRITE,
                                       MAP_SHARED, fd_, offset);
                if (address == MAP_FAILED) {
                    throw system_error("mmap");
                }
                buffers_[index].planes[plane] = {address, length};
            }
        }
    } catch (...) {
        cleanup();
        throw;
    }

    std::cout << "Actual format      : " << fourcc_to_string(actual_fourcc_) << '\n'
              << "Actual width       : " << actual_width_ << '\n'
              << "Actual height      : " << actual_height_ << '\n'
              << "Plane count        : " << size_image_.size() << '\n';
    for (std::size_t i = 0; i < size_image_.size(); ++i) {
        std::cout << "Plane " << i << " bytes/line : " << bytes_per_line_[i]
                  << '\n'
                  << "Plane " << i << " image size : " << size_image_[i]
                  << '\n';
    }
    std::cout << "Buffer count       : " << buffers_.size() << '\n';
}

void V4L2Camera::queue_all_buffers()
{
    for (std::uint32_t index = 0; index < buffers_.size(); ++index) {
        v4l2_buffer buffer{};
        std::array<v4l2_plane, VIDEO_MAX_PLANES> planes{};
        buffer.type = buffer_type_;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = index;
        if (multi_planar_) {
            buffer.length =
                static_cast<std::uint32_t>(buffers_[index].planes.size());
            buffer.m.planes = planes.data();
        }
        if (xioctl(fd_, VIDIOC_QBUF, &buffer) < 0) {
            throw system_error("VIDIOC_QBUF initial");
        }
    }
}

void V4L2Camera::start_streaming()
{
    auto type = buffer_type_;
    if (xioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
        throw system_error("VIDIOC_STREAMON");
    }
    streaming_ = true;
}

void V4L2Camera::initialize()
{
    open_device();
    query_capabilities();
    enumerate_formats();
    get_format("Current");
    set_requested_format();
    get_format("Accepted");
    request_and_map_buffers();
    queue_all_buffers();
    start_streaming();
}

std::filesystem::path V4L2Camera::save_frame_data(
    const std::vector<std::uint8_t>& data,
    std::uint32_t sequence,
    std::uint64_t frame_number) const
{
    std::error_code error;
    std::filesystem::create_directories(config_.output, error);
    if (error) {
        throw std::runtime_error("create output directory " +
                                 config_.output.string() + ": " +
                                 error.message());
    }

    std::ostringstream name;
    name << "frame_" << std::setfill('0') << std::setw(6) << frame_number
         << "_seq_" << sequence << '_' << actual_width_ << 'x'
         << actual_height_ << '_' << fourcc_to_string(actual_fourcc_) << ".raw";
    const auto path = config_.output / name.str();
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot open output file: " + path.string());
    }
    output.write(reinterpret_cast<const char*>(data.data()),
                 static_cast<std::streamsize>(data.size()));
    if (!output) {
        throw std::runtime_error("failed while writing: " + path.string());
    }
    output.close();
    if (!output) {
        throw std::runtime_error("failed to close output file: " + path.string());
    }
    return path;
}

V4L2Camera::CaptureStats V4L2Camera::capture(
    const std::function<bool()>& should_stop)
{
    struct PendingSavedFrame {
        std::vector<std::uint8_t> data;
        std::uint32_t sequence{};
        std::uint64_t frame_number{};
    };

    CaptureStats stats;
    std::vector<PendingSavedFrame> pending_saved_frames;
    bool have_sequence = false;
    std::uint32_t previous_sequence = 0;
    auto started = std::chrono::steady_clock::now();
    auto previous_frame_time = started;

    while (!should_stop() &&
           (config_.frames == 0 || stats.total_frames < config_.frames)) {
        pollfd descriptor{fd_, POLLIN | POLLPRI, 0};
        int poll_result;
        do {
            poll_result = ::poll(&descriptor, 1, config_.poll_timeout_ms);
        } while (poll_result < 0 && errno == EINTR && !should_stop());

        if (should_stop()) break;
        if (poll_result < 0) throw system_error("poll");
        if (poll_result == 0) {
            std::cerr << "WARNING: poll timeout after "
                      << config_.poll_timeout_ms << " ms\n";
            continue;
        }
        if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            throw std::runtime_error("poll reported camera error, revents=" +
                                     std::to_string(descriptor.revents));
        }

        v4l2_buffer buffer{};
        std::array<v4l2_plane, VIDEO_MAX_PLANES> planes{};
        buffer.type = buffer_type_;
        buffer.memory = V4L2_MEMORY_MMAP;
        if (multi_planar_) {
            buffer.length = static_cast<std::uint32_t>(planes.size());
            buffer.m.planes = planes.data();
        }
        if (xioctl(fd_, VIDIOC_DQBUF, &buffer) < 0) {
            if (errno == EAGAIN) continue;
            throw system_error("VIDIOC_DQBUF");
        }

        bool must_requeue = true;
        std::vector<std::uint8_t> pending_save;
        std::uint32_t pending_sequence = 0;
        std::uint64_t pending_frame_number = 0;
        try {
            if (buffer.index >= buffers_.size()) {
                throw std::runtime_error("driver returned invalid buffer index");
            }
            const auto& mapped = buffers_[buffer.index];
            const std::uint32_t plane_count =
                multi_planar_ ? buffer.length : 1U;
            if (plane_count == 0 || plane_count > mapped.planes.size()) {
                throw std::runtime_error("driver returned invalid plane count");
            }

            FrameView frame;
            frame.index = buffer.index;
            frame.sequence = buffer.sequence;
            frame.timestamp = buffer.timestamp;
            frame.planes.reserve(plane_count);
            for (std::uint32_t plane = 0; plane < plane_count; ++plane) {
                const std::size_t bytes_used =
                    multi_planar_ ? planes[plane].bytesused : buffer.bytesused;
                const std::size_t data_offset =
                    multi_planar_ ? planes[plane].data_offset : 0U;
                const auto& mapping = mapped.planes[plane];
                if (data_offset > mapping.length ||
                    bytes_used > mapping.length - data_offset) {
                    throw std::runtime_error(
                        "bytesused/data_offset exceeds mapped buffer");
                }
                const auto* base =
                    static_cast<const std::uint8_t*>(mapping.address);
                frame.planes.push_back({base + data_offset, bytes_used});
            }

            ++stats.total_frames;
            stats.total_bytes += frame.total_bytes();
            std::uint64_t new_gaps = 0;
            bool duplicate_sequence = false;
            bool out_of_order_sequence = false;
            if (have_sequence) {
                const std::uint32_t delta = frame.sequence - previous_sequence;
                if (delta == 0U) {
                    duplicate_sequence = true;
                    ++stats.duplicate_sequences;
                } else if (delta == 1U) {
                    // Expected next sequence, including uint32 wraparound.
                } else if (delta < 0x80000000U) {
                    new_gaps = static_cast<std::uint64_t>(delta - 1U);
                    stats.sequence_gaps += new_gaps;
                } else {
                    out_of_order_sequence = true;
                    ++stats.out_of_order_sequences;
                }
            }
            previous_sequence = frame.sequence;
            have_sequence = true;

            if (stats.total_frames >= config_.save_start &&
                pending_saved_frames.size() < config_.save_count) {
                pending_save.reserve(frame.total_bytes());
                for (const auto& plane : frame.planes) {
                    pending_save.insert(pending_save.end(), plane.data,
                                        plane.data + plane.bytes_used);
                }
                pending_sequence = frame.sequence;
                pending_frame_number = stats.total_frames;
            }

            const auto now = std::chrono::steady_clock::now();
            const double instant_seconds =
                std::chrono::duration<double>(now - previous_frame_time).count();
            const double elapsed =
                std::chrono::duration<double>(now - started).count();
            previous_frame_time = now;
            const bool log_frame =
                stats.total_frames == 1 ||
                new_gaps != 0 ||
                duplicate_sequence ||
                out_of_order_sequence ||
                (config_.log_interval != 0 &&
                 stats.total_frames % config_.log_interval == 0);
            if (log_frame) {
                std::cout << std::fixed << std::setprecision(2)
                          << "frame=" << stats.total_frames
                          << " sequence=" << frame.sequence
                          << " bytes=" << frame.total_bytes()
                          << " timestamp=" << timeval_string(frame.timestamp)
                          << " instant_fps="
                          << (instant_seconds > 0.0 ? 1.0 / instant_seconds : 0.0)
                          << " average_fps="
                          << (elapsed > 0.0
                                  ? static_cast<double>(stats.total_frames) /
                                        elapsed
                                  : 0.0)
                          << " dropped_or_sequence_gap=" << new_gaps
                          << " duplicate_sequence="
                          << (duplicate_sequence ? 1 : 0)
                          << " out_of_order_sequence="
                          << (out_of_order_sequence ? 1 : 0) << '\n';
            }
        } catch (...) {
            if (xioctl(fd_, VIDIOC_QBUF, &buffer) < 0) {
                std::cerr << "ERROR: failed to requeue buffer during unwind: "
                          << std::strerror(errno) << '\n';
            }
            must_requeue = false;
            throw;
        }

        if (must_requeue && xioctl(fd_, VIDIOC_QBUF, &buffer) < 0) {
            throw system_error("VIDIOC_QBUF");
        }

        // Keep only the requested, bounded number of frame copies. File I/O
        // is deferred until after STREAMOFF so it cannot stall DQBUF/QBUF.
        if (!pending_save.empty()) {
            pending_saved_frames.push_back(
                {std::move(pending_save), pending_sequence,
                 pending_frame_number});
        }
    }

    const auto finished = std::chrono::steady_clock::now();
    stats.elapsed_seconds =
        std::chrono::duration<double>(finished - started).count();
    stats.average_fps =
        stats.elapsed_seconds > 0.0
            ? static_cast<double>(stats.total_frames) / stats.elapsed_seconds
            : 0.0;

    stop();
    for (const auto& saved : pending_saved_frames) {
        try {
            const auto path =
                save_frame_data(saved.data, saved.sequence, saved.frame_number);
            stats.saved_paths.push_back(path);
            std::cout << "Saved frame        : " << path << " ("
                      << saved.data.size() << " bytes)\n";
        } catch (const std::exception& error) {
            std::cerr << "ERROR: " << error.what() << '\n';
        }
    }
    return stats;
}

void V4L2Camera::stop() noexcept
{
    if (fd_ >= 0 && streaming_) {
        auto type = buffer_type_;
        if (xioctl(fd_, VIDIOC_STREAMOFF, &type) < 0 && errno != ENODEV) {
            std::cerr << "WARNING: VIDIOC_STREAMOFF: "
                      << std::strerror(errno) << '\n';
        }
        streaming_ = false;
    }
}

void V4L2Camera::cleanup() noexcept
{
    stop();
    for (auto& buffer : buffers_) {
        for (auto& plane : buffer.planes) {
            if (plane.address != MAP_FAILED) {
                if (::munmap(plane.address, plane.length) < 0) {
                    std::cerr << "WARNING: munmap: " << std::strerror(errno)
                              << '\n';
                }
                plane.address = MAP_FAILED;
                plane.length = 0;
            }
        }
    }
    buffers_.clear();
    if (fd_ >= 0) {
        if (::close(fd_) < 0) {
            std::cerr << "WARNING: close: " << std::strerror(errno) << '\n';
        }
        fd_ = -1;
    }
}
