#pragma once
#include <cstdint>
#include <vector>

std::vector<std::uint8_t> nv12_to_jpeg(const std::vector<std::uint8_t>& nv12,
                                       int width, int height, int quality);
