#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

struct CameraConfig {
    std::string device{"/dev/video-camera0"};
    std::uint32_t width{2112};
    std::uint32_t height{1568};
    std::string format{"NV12"};
    std::uint64_t frames{300};
    std::uint32_t save_count{3};
    std::uint64_t save_start{1};
    std::filesystem::path output{"/tmp/zx-camera"};
    std::uint32_t buffer_count{4};
    int poll_timeout_ms{2000};
    std::uint32_t log_interval{100};
};

CameraConfig load_config_file(const std::filesystem::path& path);
CameraConfig parse_arguments(int argc, char** argv);
void print_usage(const char* program);
