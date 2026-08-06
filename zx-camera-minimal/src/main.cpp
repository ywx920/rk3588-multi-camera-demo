#include "camera_config.h"
#include "v4l2_camera.h"

#include <csignal>
#include <iomanip>
#include <iostream>

namespace {

volatile std::sig_atomic_t g_stop_requested = 0;

extern "C" void handle_signal(int) noexcept
{
    g_stop_requested = 1;
}

bool install_signal_handler()
{
    struct sigaction action {};
    action.sa_handler = handle_signal;
    if (sigemptyset(&action.sa_mask) < 0) return false;
    action.sa_flags = 0;
    return sigaction(SIGINT, &action, nullptr) == 0 &&
           sigaction(SIGTERM, &action, nullptr) == 0;
}

void print_summary(const V4L2Camera::CaptureStats& stats)
{
    std::cout << "\n===== Capture summary =====\n"
              << "Total frames       : " << stats.total_frames << '\n'
              << "Total bytes        : " << stats.total_bytes << '\n'
              << std::fixed << std::setprecision(3)
              << "Elapsed time       : " << stats.elapsed_seconds << " s\n"
              << std::setprecision(2)
              << "Average FPS        : " << stats.average_fps << '\n'
              << "Sequence gaps      : " << stats.sequence_gaps << '\n'
              << "Duplicate sequences: " << stats.duplicate_sequences << '\n'
              << "Out-of-order seq   : " << stats.out_of_order_sequences << '\n'
              << "Saved frame paths  :";
    if (stats.saved_paths.empty()) {
        std::cout << " none\n";
    } else {
        std::cout << '\n';
        for (const auto& path : stats.saved_paths) {
            std::cout << "  " << path << '\n';
        }
    }
}

}  // namespace

int main(int argc, char** argv)
{
    try {
        const CameraConfig config = parse_arguments(argc, argv);
        if (!install_signal_handler()) {
            std::cerr << "ERROR: failed to install signal handlers\n";
            return 1;
        }

        V4L2Camera camera(config);
        camera.initialize();
        const auto stats =
            camera.capture([] { return g_stop_requested != 0; });
        camera.stop();
        print_summary(stats);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FATAL: " << error.what() << '\n';
        return 1;
    }
}
