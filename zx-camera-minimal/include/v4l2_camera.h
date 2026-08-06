#pragma once

#include "camera_config.h"

#include <linux/videodev2.h>
#include <sys/mman.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

class V4L2Camera {
public:
    struct PlaneView {
        const std::uint8_t* data{};
        std::size_t bytes_used{};
    };

    struct FrameView {
        std::uint32_t index{};
        std::uint32_t sequence{};
        timeval timestamp{};
        std::vector<PlaneView> planes;

        std::size_t total_bytes() const noexcept;
    };

    struct CaptureStats {
        std::uint64_t total_frames{};
        std::uint64_t total_bytes{};
        std::uint64_t sequence_gaps{};
        std::uint64_t duplicate_sequences{};
        std::uint64_t out_of_order_sequences{};
        double elapsed_seconds{};
        double average_fps{};
        std::vector<std::filesystem::path> saved_paths;
    };

    explicit V4L2Camera(CameraConfig config);
    ~V4L2Camera() noexcept;

    V4L2Camera(const V4L2Camera&) = delete;
    V4L2Camera& operator=(const V4L2Camera&) = delete;

    void initialize();
    CaptureStats capture(const std::function<bool()>& should_stop);
    void stop() noexcept;

private:
    struct Mapping {
        void* address{MAP_FAILED};
        std::size_t length{};
    };

    struct Buffer {
        std::vector<Mapping> planes;
    };

    static int xioctl(int fd, unsigned long request, void* argument);
    static std::string fourcc_to_string(std::uint32_t fourcc);
    static std::uint32_t string_to_fourcc(const std::string& text);
    static std::string capability_string(std::uint32_t caps);

    void open_device();
    void query_capabilities();
    void enumerate_formats() const;
    void get_format(const char* label);
    void set_requested_format();
    void request_and_map_buffers();
    void queue_all_buffers();
    void start_streaming();
    void cleanup() noexcept;
    std::filesystem::path save_frame_data(
        const std::vector<std::uint8_t>& data,
        std::uint32_t sequence,
        std::uint64_t frame_number) const;

    CameraConfig config_;
    int fd_{-1};
    v4l2_buf_type buffer_type_{V4L2_BUF_TYPE_VIDEO_CAPTURE};
    bool multi_planar_{false};
    bool streaming_{false};
    std::uint32_t capabilities_{};
    std::uint32_t actual_width_{};
    std::uint32_t actual_height_{};
    std::uint32_t actual_fourcc_{};
    std::vector<std::uint32_t> bytes_per_line_;
    std::vector<std::uint32_t> size_image_;
    std::vector<Buffer> buffers_;
};
