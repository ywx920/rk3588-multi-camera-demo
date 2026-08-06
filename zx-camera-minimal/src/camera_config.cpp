#include "camera_config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {

std::string trim(std::string text)
{
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    text.erase(text.begin(),
               std::find_if(text.begin(), text.end(), not_space));
    text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(),
               text.end());
    return text;
}

std::uint64_t parse_u64(const std::string& name, const std::string& value)
{
    std::size_t used = 0;
    unsigned long long result = 0;
    try {
        result = std::stoull(value, &used, 10);
    } catch (const std::exception&) {
        throw std::runtime_error("invalid value for " + name + ": " + value);
    }
    if (used != value.size()) {
        throw std::runtime_error("invalid value for " + name + ": " + value);
    }
    return static_cast<std::uint64_t>(result);
}

std::uint32_t parse_u32(const std::string& name, const std::string& value)
{
    const auto result = parse_u64(name, value);
    if (result > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("value out of range for " + name);
    }
    return static_cast<std::uint32_t>(result);
}

int parse_positive_int(const std::string& name, const std::string& value)
{
    const auto result = parse_u64(name, value);
    if (result == 0 ||
        result > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("invalid positive value for " + name);
    }
    return static_cast<int>(result);
}

void apply_value(CameraConfig& config,
                 const std::string& key,
                 const std::string& value)
{
    if (key == "device") {
        config.device = value;
    } else if (key == "width") {
        config.width = parse_u32(key, value);
    } else if (key == "height") {
        config.height = parse_u32(key, value);
    } else if (key == "format") {
        config.format = value;
    } else if (key == "frames") {
        config.frames = parse_u64(key, value);
    } else if (key == "save_count") {
        config.save_count = parse_u32(key, value);
    } else if (key == "save_start") {
        config.save_start = parse_u64(key, value);
    } else if (key == "output") {
        config.output = value;
    } else if (key == "buffer_count") {
        config.buffer_count = parse_u32(key, value);
    } else if (key == "poll_timeout_ms") {
        config.poll_timeout_ms = parse_positive_int(key, value);
    } else if (key == "log_interval") {
        config.log_interval = parse_u32(key, value);
    } else {
        throw std::runtime_error("unknown configuration key: " + key);
    }
}

std::string require_value(int argc, char** argv, int& index)
{
    if (index + 1 >= argc) {
        throw std::runtime_error(std::string("missing value after ") + argv[index]);
    }
    ++index;
    return argv[index];
}

}  // namespace

CameraConfig load_config_file(const std::filesystem::path& path)
{
    CameraConfig config;
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open config file: " + path.string());
    }

    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        line = trim(line);
        if (line.empty() || line.front() == '#' || line.front() == ';' ||
            line.front() == '[') {
            continue;
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            throw std::runtime_error("invalid config line " +
                                     std::to_string(line_number));
        }
        try {
            apply_value(config, trim(line.substr(0, separator)),
                        trim(line.substr(separator + 1)));
        } catch (const std::exception& error) {
            throw std::runtime_error(path.string() + ":" +
                                     std::to_string(line_number) + ": " +
                                     error.what());
        }
    }
    return config;
}

CameraConfig parse_arguments(int argc, char** argv)
{
    CameraConfig config;

    // Load the file first so command-line options always take precedence.
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--config") {
            config = load_config_file(require_value(argc, argv, i));
        }
    }

    for (int i = 1; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--help" || option == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        } else if (option == "--config") {
            (void)require_value(argc, argv, i);
        } else if (option == "--device") {
            config.device = require_value(argc, argv, i);
        } else if (option == "--width") {
            config.width = parse_u32(option, require_value(argc, argv, i));
        } else if (option == "--height") {
            config.height = parse_u32(option, require_value(argc, argv, i));
        } else if (option == "--format") {
            config.format = require_value(argc, argv, i);
        } else if (option == "--frames") {
            config.frames = parse_u64(option, require_value(argc, argv, i));
        } else if (option == "--save-count") {
            config.save_count = parse_u32(option, require_value(argc, argv, i));
        } else if (option == "--save-start") {
            config.save_start = parse_u64(option, require_value(argc, argv, i));
        } else if (option == "--output") {
            config.output = require_value(argc, argv, i);
        } else if (option == "--buffer-count") {
            config.buffer_count =
                parse_u32(option, require_value(argc, argv, i));
        } else if (option == "--poll-timeout-ms") {
            config.poll_timeout_ms =
                parse_positive_int(option, require_value(argc, argv, i));
        } else if (option == "--log-interval") {
            config.log_interval =
                parse_u32(option, require_value(argc, argv, i));
        } else {
            throw std::runtime_error("unknown argument: " + option);
        }
    }

    if (config.device.empty() || config.width == 0 || config.height == 0) {
        throw std::runtime_error("device, width and height must be non-empty/non-zero");
    }
    if (config.format.size() != 4) {
        throw std::runtime_error("format must be exactly four characters, e.g. NV12");
    }
    if (config.buffer_count < 4) {
        throw std::runtime_error("buffer-count must be at least 4");
    }
    if (config.save_start == 0) {
        throw std::runtime_error("save-start must be at least 1");
    }
    return config;
}

void print_usage(const char* program)
{
    std::cout
        << "Usage: " << program << " [options]\n"
        << "  --config PATH          Read key=value configuration\n"
        << "  --device PATH          V4L2 node (default /dev/video-camera0)\n"
        << "  --width N              Requested width (default 2112)\n"
        << "  --height N             Requested height (default 1568)\n"
        << "  --format FOURCC        Requested format (default NV12)\n"
        << "  --frames N             Total frames; 0 runs until Ctrl+C\n"
        << "  --save-count N         Save the first N frames\n"
        << "  --save-start N         First 1-based frame eligible for saving\n"
        << "  --output PATH          Raw frame output directory\n"
        << "  --buffer-count N       MMAP buffers, minimum 4\n"
        << "  --poll-timeout-ms N    Per-poll timeout\n"
        << "  --log-interval N       Print every N frames; 0 disables periodic logs\n"
        << "  -h, --help             Show this help\n";
}
