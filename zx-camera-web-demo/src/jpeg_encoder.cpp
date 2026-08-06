#include "jpeg_encoder.h"

#include <cstdio>
#include <jpeglib.h>

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <vector>

namespace {
std::uint8_t clamp(int value)
{
    return static_cast<std::uint8_t>(std::max(0, std::min(255, value)));
}
}

std::vector<std::uint8_t> nv12_to_jpeg(const std::vector<std::uint8_t>& nv12,
                                       int width, int height, int quality)
{
    const std::size_t y_size = static_cast<std::size_t>(width) * height;
    if (nv12.size() < y_size * 3U / 2U) throw std::runtime_error("short NV12 frame");
    std::vector<std::uint8_t> rgb(y_size * 3U);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int yy = nv12[static_cast<std::size_t>(y) * width + x];
            const std::size_t uv = y_size + static_cast<std::size_t>(y / 2) * width + (x & ~1);
            const int u = static_cast<int>(nv12[uv]) - 128;
            const int v = static_cast<int>(nv12[uv + 1]) - 128;
            const int c = std::max(0, yy - 16);
            const std::size_t out = (static_cast<std::size_t>(y) * width + x) * 3U;
            rgb[out] = clamp((298 * c + 409 * v + 128) >> 8);
            rgb[out + 1] = clamp((298 * c - 100 * u - 208 * v + 128) >> 8);
            rgb[out + 2] = clamp((298 * c + 516 * u + 128) >> 8);
        }
    }
    jpeg_compress_struct compressor{};
    jpeg_error_mgr errors{};
    compressor.err = jpeg_std_error(&errors);
    jpeg_create_compress(&compressor);
    unsigned char* output = nullptr;
    unsigned long output_size = 0;
    jpeg_mem_dest(&compressor, &output, &output_size);
    compressor.image_width = width;
    compressor.image_height = height;
    compressor.input_components = 3;
    compressor.in_color_space = JCS_RGB;
    jpeg_set_defaults(&compressor);
    jpeg_set_quality(&compressor, quality, TRUE);
    jpeg_start_compress(&compressor, TRUE);
    while (compressor.next_scanline < compressor.image_height) {
        JSAMPROW row = rgb.data() + static_cast<std::size_t>(compressor.next_scanline) * width * 3U;
        (void)jpeg_write_scanlines(&compressor, &row, 1);
    }
    jpeg_finish_compress(&compressor);
    std::vector<std::uint8_t> result(output, output + output_size);
    std::free(output);
    jpeg_destroy_compress(&compressor);
    return result;
}
