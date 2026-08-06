#pragma once

#include <linux/videodev2.h>
#include <sys/mman.h>

#include <cstdint>
#include <string>
#include <vector>

class V4L2Camera final {
public:
    struct Frame {
        std::vector<std::uint8_t> data;
        std::uint32_t sequence{};
    };

    V4L2Camera() = default;
    ~V4L2Camera() noexcept;
    V4L2Camera(const V4L2Camera&) = delete;
    V4L2Camera& operator=(const V4L2Camera&) = delete;

    void open(const std::string& device, int width, int height,
              const std::string& fourcc, unsigned int buffer_count = 4);
    bool capture(Frame& frame, int timeout_ms, bool copy_data = true);
    void close() noexcept;

    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    std::string format() const;

private:
    struct Mapping { void* address{MAP_FAILED}; std::size_t length{}; };
    struct Buffer { std::vector<Mapping> planes; };

    static int xioctl(int fd, unsigned long request, void* argument);
    static std::uint32_t parse_fourcc(const std::string& text);
    void queue(unsigned int index);

    int fd_{-1};
    v4l2_buf_type type_{V4L2_BUF_TYPE_VIDEO_CAPTURE};
    bool multi_planar_{false};
    bool streaming_{false};
    int width_{};
    int height_{};
    std::uint32_t fourcc_{};
    std::vector<Buffer> buffers_;
};
