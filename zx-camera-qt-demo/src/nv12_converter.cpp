#include "nv12_converter.h"

#include <algorithm>
#include <mutex>
#include <stdexcept>

#include <QDebug>

#ifdef ZX_CAMERA_HAVE_RGA
#include <rga/im2d.h>
#endif

namespace {

std::once_flag converter_backend_once;

std::uint8_t clamp_u8(int value)
{
    return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

}  // namespace

QImage Nv12Converter::to_rgb(const std::vector<std::uint8_t>& nv12,
                             int width,
                             int height)
{
    if (width <= 0 || height <= 0 || (width % 2) != 0 ||
        (height % 2) != 0) {
        throw std::runtime_error("NV12 width and height must be positive/even");
    }
    const auto expected =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
        3U / 2U;
    if (nv12.size() != expected) {
        throw std::runtime_error("NV12 buffer size does not match geometry");
    }

    QImage rgb(width, height, QImage::Format_RGB888);
    if (rgb.isNull()) throw std::runtime_error("failed to allocate RGB image");

    const std::size_t y_size =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (int row = 0; row < height; ++row) {
        auto* output = rgb.scanLine(row);
        const auto y_row =
            static_cast<std::size_t>(row) * static_cast<std::size_t>(width);
        const auto uv_row = y_size +
            static_cast<std::size_t>(row / 2) *
                static_cast<std::size_t>(width);
        for (int col = 0; col < width; ++col) {
            const int y = std::max(
                0, static_cast<int>(nv12[y_row +
                    static_cast<std::size_t>(col)]) - 16);
            const auto uv_index =
                uv_row + static_cast<std::size_t>(col & ~1);
            const int u = static_cast<int>(nv12[uv_index]) - 128;
            const int v = static_cast<int>(nv12[uv_index + 1U]) - 128;
            const int c = 298 * y;
            output[col * 3] = clamp_u8((c + 409 * v + 128) >> 8);
            output[col * 3 + 1] =
                clamp_u8((c - 100 * u - 208 * v + 128) >> 8);
            output[col * 3 + 2] =
                clamp_u8((c + 516 * u + 128) >> 8);
        }
    }
    return rgb;
}

QImage Nv12Converter::to_rgb_scaled(
    const std::vector<std::uint8_t>& nv12, int width, int height,
    int output_width, int output_height)
{
    if (output_width <= 0 || output_height <= 0 ||
        (output_width % 2) != 0 || (output_height % 2) != 0) {
        throw std::runtime_error("scaled RGB dimensions must be positive/even");
    }
    const auto expected = static_cast<std::size_t>(width) *
                          static_cast<std::size_t>(height) * 3U / 2U;
    if (nv12.size() != expected) {
        throw std::runtime_error("NV12 buffer size does not match geometry");
    }
#ifdef ZX_CAMERA_HAVE_RGA
    QImage rgb(output_width, output_height, QImage::Format_RGB888);
    if (rgb.isNull()) throw std::runtime_error("failed to allocate RGA RGB image");
    auto source = wrapbuffer_virtualaddr_t(
        const_cast<std::uint8_t*>(nv12.data()), width, height,
        width, height, RK_FORMAT_YCbCr_420_SP);
    auto destination = wrapbuffer_virtualaddr_t(
        rgb.bits(), output_width, output_height,
        output_width, output_height, RK_FORMAT_RGB_888);
    const IM_STATUS status = imresize_t(
        source, destination, 0.0, 0.0, INTER_LINEAR, 1);
    if (status == IM_STATUS_SUCCESS) {
        std::call_once(converter_backend_once, [] {
            qInfo() << "NV12 conversion backend: RGA resize + color conversion";
        });
        return rgb;
    }
    std::call_once(converter_backend_once, [status] {
        qWarning() << "RGA conversion unavailable at runtime, using CPU fallback:"
                   << static_cast<int>(status);
    });
#else
    std::call_once(converter_backend_once, [] {
        qInfo() << "NV12 conversion backend: CPU fallback";
    });
#endif
    return to_rgb(nv12, width, height).scaled(
        output_width, output_height, Qt::IgnoreAspectRatio,
        Qt::FastTransformation);
}

QImage Nv12Converter::to_rgb_scaled_buffer(
    const std::uint8_t* nv12, std::size_t size, int dma_fd,
    int width, int height, int output_width, int output_height)
{
    const auto expected = static_cast<std::size_t>(width) *
                          static_cast<std::size_t>(height) * 3U / 2U;
    if (nv12 == nullptr || size < expected)
        throw std::runtime_error("borrowed NV12 buffer is too small");
#ifdef ZX_CAMERA_HAVE_RGA
    if (dma_fd >= 0) {
        QImage rgb(output_width, output_height, QImage::Format_RGB888);
        auto source = wrapbuffer_fd_t(dma_fd, width, height, width, height,
                                      RK_FORMAT_YCbCr_420_SP);
        auto destination = wrapbuffer_virtualaddr_t(
            rgb.bits(), output_width, output_height,
            output_width, output_height, RK_FORMAT_RGB_888);
        const IM_STATUS status = imresize_t(
            source, destination, 0.0, 0.0, INTER_LINEAR, 1);
        if (status == IM_STATUS_SUCCESS) {
            std::call_once(converter_backend_once, [] {
                qInfo() << "NV12 conversion backend: V4L2 DMABUF -> RGA";
            });
            return rgb;
        }
    }
#endif
    std::vector<std::uint8_t> copy(nv12, nv12 + expected);
    return to_rgb_scaled(copy, width, height, output_width, output_height);
}
