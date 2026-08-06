#pragma once

#include <QImage>

#include <cstdint>
#include <vector>

class Nv12Converter {
public:
    static QImage to_rgb(const std::vector<std::uint8_t>& nv12,
                         int width,
                         int height);
    static QImage to_rgb_scaled(const std::vector<std::uint8_t>& nv12,
                                int width,
                                int height,
                                int output_width,
                                int output_height);
    static QImage to_rgb_scaled_buffer(const std::uint8_t* nv12,
                                       std::size_t size,
                                       int dma_fd,
                                       int width,
                                       int height,
                                       int output_width,
                                       int output_height);
};
