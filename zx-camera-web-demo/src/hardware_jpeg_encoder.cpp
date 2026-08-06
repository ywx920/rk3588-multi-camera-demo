#include "hardware_jpeg_encoder.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#ifdef ZX_CAMERA_HAVE_MPP
#include <rockchip/mpp_buffer.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_packet.h>
#include <rockchip/rk_mpi.h>
#include <rockchip/rk_venc_cfg.h>
#endif

class HardwareJpegEncoder::Impl {
public:
    ~Impl() { close(); }

    std::vector<std::uint8_t> encode(const QImage& source, int quality)
    {
#ifndef ZX_CAMERA_HAVE_MPP
        (void)source;
        (void)quality;
        throw std::runtime_error("Rockchip MPP support was not compiled in");
#else
        if (disabled_) throw std::runtime_error(error_);
        QImage image = source.format() == QImage::Format_RGB888
            ? source : source.convertToFormat(QImage::Format_RGB888);
        if (!context_ || image.width() != width_ || image.height() != height_)
            initialize(image.width(), image.height(), quality);

        auto* destination = static_cast<std::uint8_t*>(
            mpp_buffer_get_ptr(frame_buffer_));
        if (!destination) return fail("MPP input buffer is null");
        mpp_buffer_sync_begin(frame_buffer_);
        std::memset(destination, 0,
                    static_cast<std::size_t>(horizontal_stride_) *
                    static_cast<std::size_t>(vertical_stride_));
        for (int row = 0; row < height_; ++row) {
            std::memcpy(destination +
                            static_cast<std::size_t>(row) * horizontal_stride_,
                        image.constScanLine(row),
                        static_cast<std::size_t>(width_) * 3U);
        }
        mpp_buffer_sync_end(frame_buffer_);

        MppFrame frame = nullptr;
        if (mpp_frame_init(&frame) != MPP_OK)
            return fail("mpp_frame_init failed");
        mpp_frame_set_width(frame, width_);
        mpp_frame_set_height(frame, height_);
        mpp_frame_set_hor_stride(frame, horizontal_stride_);
        mpp_frame_set_ver_stride(frame, vertical_stride_);
        mpp_frame_set_fmt(frame, MPP_FMT_RGB888);
        mpp_frame_set_buffer(frame, frame_buffer_);
        const MPP_RET put_result = api_->encode_put_frame(context_, frame);
        mpp_frame_deinit(&frame);
        if (put_result != MPP_OK) return fail("MPP encode_put_frame failed");

        std::vector<std::uint8_t> encoded;
        bool frame_complete = false;
        for (int part = 0; part < 64 && !frame_complete; ++part) {
            MppPacket packet = nullptr;
            if (api_->encode_get_packet(context_, &packet) != MPP_OK ||
                !packet)
                return fail("MPP encode_get_packet failed");
            const auto* data = static_cast<const std::uint8_t*>(
                mpp_packet_get_pos(packet));
            const std::size_t size = mpp_packet_get_length(packet);
            if (data && size > 0U)
                encoded.insert(encoded.end(), data, data + size);
            const bool partition = mpp_packet_is_partition(packet) != 0;
            const bool end_of_image = mpp_packet_is_eoi(packet) != 0;
            frame_complete = !partition || end_of_image;
            mpp_packet_deinit(&packet);
        }
        if (!frame_complete) return fail("MPP JPEG partition limit exceeded");
        if (encoded.size() < 4U || encoded[0] != 0xffU ||
            encoded[1] != 0xd8U)
            return fail("MPP returned an invalid JPEG packet");
        auto eoi = encoded.end();
        bool eoi_found = false;
        for (auto it = encoded.end() - 1; it != encoded.begin(); --it) {
            if (*(it - 1) == 0xffU && *it == 0xd9U) {
                eoi = it + 1;
                eoi_found = true;
                break;
            }
        }
        if (!eoi_found)
            return fail("MPP JPEG packet has no EOI marker");
        encoded.erase(eoi, encoded.end());
        if (!validated_) {
            const QImage decoded = QImage::fromData(
                encoded.data(), static_cast<int>(encoded.size()), "JPG");
            if (decoded.isNull() || decoded.width() != width_ ||
                decoded.height() != height_)
                return fail("MPP RGB888 JPEG failed decode validation");
            validated_ = true;
        }
        return encoded;
#endif
    }

    bool available() const
    {
#ifdef ZX_CAMERA_HAVE_MPP
        return !disabled_;
#else
        return false;
#endif
    }
    const std::string& error() const { return error_; }

private:
#ifdef ZX_CAMERA_HAVE_MPP
    static int align_up(int value, int alignment)
    {
        return (value + alignment - 1) / alignment * alignment;
    }

    void initialize(int width, int height, int quality)
    {
        close();
        width_ = width;
        height_ = height;
        horizontal_stride_ = align_up(width * 3, 64);
        vertical_stride_ = align_up(height, 16);
        validated_ = false;
        MppPollType timeout = MPP_POLL_BLOCK;
        MppEncCfg config = nullptr;
        if (mpp_create(&context_, &api_) != MPP_OK || !context_ || !api_)
            return disable("mpp_create failed");
        if (api_->control(context_, MPP_SET_OUTPUT_TIMEOUT, &timeout) != MPP_OK)
            return disable("MPP_SET_OUTPUT_TIMEOUT failed");
        if (mpp_init(context_, MPP_CTX_ENC, MPP_VIDEO_CodingMJPEG) != MPP_OK)
            return disable("mpp_init MJPEG failed");
        if (mpp_enc_cfg_init(&config) != MPP_OK || !config)
            return disable("mpp_enc_cfg_init failed");
        if (api_->control(context_, MPP_ENC_GET_CFG, config) != MPP_OK) {
            mpp_enc_cfg_deinit(config);
            return disable("MPP_ENC_GET_CFG failed");
        }
        mpp_enc_cfg_set_s32(config, "prep:width", width_);
        mpp_enc_cfg_set_s32(config, "prep:height", height_);
        mpp_enc_cfg_set_s32(config, "prep:hor_stride", horizontal_stride_);
        mpp_enc_cfg_set_s32(config, "prep:ver_stride", vertical_stride_);
        mpp_enc_cfg_set_s32(config, "prep:format", MPP_FMT_RGB888);
        mpp_enc_cfg_set_s32(config, "rc:mode", MPP_ENC_RC_MODE_FIXQP);
        mpp_enc_cfg_set_s32(config, "jpeg:q_factor",
                            std::clamp(quality, 1, 99));
        const MPP_RET config_result =
            api_->control(context_, MPP_ENC_SET_CFG, config);
        mpp_enc_cfg_deinit(config);
        if (config_result != MPP_OK) return disable("MPP_ENC_SET_CFG failed");
        if (mpp_buffer_group_get_internal(
                &buffer_group_, MPP_BUFFER_TYPE_DRM) != MPP_OK)
            return disable("MPP buffer group allocation failed");
        const std::size_t bytes =
            static_cast<std::size_t>(horizontal_stride_) * vertical_stride_;
        if (mpp_buffer_get(buffer_group_, &frame_buffer_, bytes) != MPP_OK)
            return disable("MPP frame buffer allocation failed");
    }

    void close()
    {
        if (frame_buffer_) mpp_buffer_put(frame_buffer_);
        frame_buffer_ = nullptr;
        if (buffer_group_) mpp_buffer_group_put(buffer_group_);
        buffer_group_ = nullptr;
        if (context_) mpp_destroy(context_);
        context_ = nullptr;
        api_ = nullptr;
    }

    std::vector<std::uint8_t> fail(const char* message)
    {
        disable(message);
        throw std::runtime_error(error_);
    }
    void disable(const char* message)
    {
        error_ = message;
        disabled_ = true;
        close();
    }

    MppCtx context_{nullptr};
    MppApi* api_{nullptr};
    MppBufferGroup buffer_group_{nullptr};
    MppBuffer frame_buffer_{nullptr};
    int width_{};
    int height_{};
    int horizontal_stride_{};
    int vertical_stride_{};
    bool validated_{false};
#else
    void close() {}
#endif
    bool disabled_{false};
    std::string error_;
};

HardwareJpegEncoder::HardwareJpegEncoder() : impl_(std::make_unique<Impl>()) {}
HardwareJpegEncoder::~HardwareJpegEncoder() = default;
std::vector<std::uint8_t> HardwareJpegEncoder::encode(
    const QImage& image, int quality)
{
    return impl_->encode(image, quality);
}
bool HardwareJpegEncoder::available() const { return impl_->available(); }
const std::string& HardwareJpegEncoder::error() const { return impl_->error(); }
