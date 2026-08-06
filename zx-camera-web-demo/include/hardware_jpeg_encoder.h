#pragma once

#include <QImage>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class HardwareJpegEncoder final {
public:
    HardwareJpegEncoder();
    ~HardwareJpegEncoder();
    HardwareJpegEncoder(const HardwareJpegEncoder&) = delete;
    HardwareJpegEncoder& operator=(const HardwareJpegEncoder&) = delete;

    std::vector<std::uint8_t> encode(const QImage& image, int quality);
    bool available() const;
    const std::string& error() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
