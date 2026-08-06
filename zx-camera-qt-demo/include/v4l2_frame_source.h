#pragma once

#include <linux/videodev2.h>
#include <sys/mman.h>

#include <cstdint>
#include <string>
#include <vector>

class V4L2FrameSource final {
public:
    struct Frame {
        std::vector<std::uint8_t> data;
        const std::uint8_t* address{nullptr};
        std::size_t bytes_used{};
        int dma_fd{-1};
        unsigned int buffer_index{};
        bool held{false};
        std::uint32_t sequence{};
        timeval timestamp{};
    };

    V4L2FrameSource() = default;
    ~V4L2FrameSource() noexcept;

    V4L2FrameSource(const V4L2FrameSource&) = delete;
    V4L2FrameSource& operator=(const V4L2FrameSource&) = delete;

    void open(const std::string& device,
              int width,
              int height,
              const std::string& fourcc,
              unsigned int buffer_count = 4);
    bool capture_one(Frame& frame, int timeout_ms, bool copy_data = true,
                     bool hold_buffer = false);
    void release(Frame& frame);
    void close() noexcept;

    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    std::string format() const;

private:
    struct Mapping {
        void* address{MAP_FAILED};
        std::size_t length{};
        int dma_fd{-1};
    };
    struct Buffer {
        std::vector<Mapping> planes;
    };

    static int xioctl(int fd, unsigned long request, void* argument);
    static std::uint32_t parse_fourcc(const std::string& text);
    void queue_buffer(unsigned int index);

    int fd_{-1};
    v4l2_buf_type type_{V4L2_BUF_TYPE_VIDEO_CAPTURE};
    bool multi_planar_{false};
    bool streaming_{false};
    int width_{0};
    int height_{0};
    std::uint32_t fourcc_{0};
    std::vector<Buffer> buffers_;
};
