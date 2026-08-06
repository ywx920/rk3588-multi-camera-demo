#include "frame_worker.h"

#include "nv12_converter.h"
#include "v4l2_frame_source.h"

#include <QImage>
#include <QFile>
#include <QTimer>

#include <algorithm>
#include <exception>

FrameWorker::FrameWorker(AppConfig config, QObject* parent)
    : QObject(parent), config_(std::move(config))
{
}

FrameWorker::~FrameWorker() = default;

bool FrameWorker::load_or_create_source()
{
    const auto frame_bytes =
        static_cast<std::size_t>(config_.width) *
        static_cast<std::size_t>(config_.height) * 3U / 2U;

    if (config_.source == SourceType::Ov13850 ||
        config_.source == SourceType::Uc325) {
        return open_v4l2_source();
    }

    if (config_.source == SourceType::Nv12File) {
        QFile input(config_.input_path);
        if (!input.open(QIODevice::ReadOnly)) {
            emit failed(QStringLiteral("Cannot open NV12 file: %1")
                            .arg(config_.input_path));
            return false;
        }
        if (input.size() < static_cast<qint64>(frame_bytes)) {
            emit failed(QStringLiteral("NV12 file is smaller than %1 bytes")
                            .arg(frame_bytes));
            return false;
        }
        const QByteArray data = input.read(static_cast<qint64>(frame_bytes));
        if (data.size() != static_cast<int>(frame_bytes)) {
            emit failed(QStringLiteral("Failed to read one complete NV12 frame"));
            return false;
        }
        nv12_.assign(reinterpret_cast<const std::uint8_t*>(data.constData()),
                     reinterpret_cast<const std::uint8_t*>(data.constData()) +
                         data.size());
        synthetic_ = false;
        emit status_changed(QStringLiteral("Loaded NV12: %1")
                                .arg(config_.input_path));
        return true;
    }

    nv12_.resize(frame_bytes);
    const auto y_size =
        static_cast<std::size_t>(config_.width) *
        static_cast<std::size_t>(config_.height);
    std::fill(nv12_.begin() + static_cast<std::ptrdiff_t>(y_size),
              nv12_.end(), static_cast<std::uint8_t>(128));
    synthetic_ = true;
    update_synthetic_frame();
    emit status_changed(QStringLiteral("Synthetic NV12 source"));
    return true;
}

bool FrameWorker::open_v4l2_source()
{
    try {
        v4l2_ = std::make_unique<V4L2FrameSource>();
        v4l2_->open(config_.device_path.toStdString(), config_.width,
                    config_.height, config_.pixel_format.toStdString());
        config_.width = v4l2_->width();
        config_.height = v4l2_->height();
        emit status_changed(
            QStringLiteral("Opened %1: %2x%3 %4")
                .arg(config_.device_path)
                .arg(config_.width)
                .arg(config_.height)
                .arg(QString::fromStdString(v4l2_->format())));
        return true;
    } catch (const std::exception& error) {
        v4l2_.reset();
        emit failed(QString::fromUtf8(error.what()));
        return false;
    }
}

void FrameWorker::start()
{
    // Keep live V4L2/ISP pipelines open across UI Stop/Start operations.  A
    // repeated Start only resumes delivery; it must not rebuild RKISP/AIQ.
    if (running_) {
        preview_enabled_ = true;
        frame_count_ = 0;
        stats_started_ = std::chrono::steady_clock::now();
        emit status_changed(QStringLiteral("Running"));
        return;
    }
    if (nv12_.empty() && !load_or_create_source()) return;

    if (timer_ == nullptr) {
        timer_ = new QTimer(this);
        timer_->setTimerType(Qt::PreciseTimer);
        connect(timer_, &QTimer::timeout, this, &FrameWorker::produce_frame);
    }

    frame_count_ = 0;
    consecutive_timeouts_ = 0;
    stats_started_ = std::chrono::steady_clock::now();
    running_ = true;
    preview_enabled_ = true;
    const bool live = config_.source == SourceType::Ov13850 ||
                      config_.source == SourceType::Uc325;
    timer_->start(live ? 0 : std::max(1, 1000 / config_.fps));
    emit status_changed(QStringLiteral("Running"));
    produce_frame();
}

void FrameWorker::stop()
{
    // UI Stop means pause.  Continue dequeuing/requeueing buffers in the
    // worker thread so that sensor, CIF, ISP and AIQ remain synchronized.
    preview_enabled_ = false;
    if (running_) emit status_changed(QStringLiteral("Paused (capture active)"));
}

void FrameWorker::shutdown_capture()
{
    preview_enabled_ = false;
    if (timer_ != nullptr) timer_->stop();
    v4l2_.reset();
    nv12_.clear();
    consecutive_timeouts_ = 0;
    running_ = false;
    emit status_changed(QStringLiteral("Stopped"));
}

void FrameWorker::update_synthetic_frame()
{
    const int shift = static_cast<int>(frame_count_ % 256U);
    for (int row = 0; row < config_.height; ++row) {
        for (int col = 0; col < config_.width; ++col) {
            const auto index =
                static_cast<std::size_t>(row) *
                    static_cast<std::size_t>(config_.width) +
                static_cast<std::size_t>(col);
            nv12_[index] = static_cast<std::uint8_t>(
                (col * 255 / std::max(1, config_.width - 1) + shift) % 256);
        }
    }
}

QImage FrameWorker::decode_v4l2_frame(
    const V4L2FrameSource::Frame& frame) const
{
    const int output_width = std::min(config_.width, config_.work_width);
    int output_height = static_cast<int>(
        static_cast<long long>(config_.height) * output_width / config_.width);
    output_height = std::max(2, output_height & ~1);
    if (config_.pixel_format == QStringLiteral("MJPG")) {
        const QByteArray encoded(
            reinterpret_cast<const char*>(frame.address),
            static_cast<int>(frame.bytes_used));
        QImage image = QImage::fromData(encoded, "JPG");
        if (image.isNull()) {
            throw std::runtime_error(
                "Qt failed to decode MJPEG; check the JPEG image plugin");
        }
        image = image.convertToFormat(QImage::Format_RGB888);
        if (image.width() != output_width || image.height() != output_height) {
            image = image.scaled(output_width, output_height,
                                 Qt::IgnoreAspectRatio,
                                 Qt::FastTransformation);
        }
        return image;
    }
    if (config_.pixel_format == QStringLiteral("NV12")) {
        return Nv12Converter::to_rgb_scaled_buffer(
            frame.address, frame.bytes_used, frame.dma_fd,
            config_.width, config_.height,
            output_width, output_height);
    }
    throw std::runtime_error("unsupported preview pixel format");
}

bool FrameWorker::processing_due()
{
    const auto now = std::chrono::steady_clock::now();
    const int rate = std::max(config_.display_fps,
                              config_.inference_enabled
                                  ? config_.inference_fps : 0);
    const auto interval = std::chrono::microseconds(
        1000000 / std::max(1, rate));
    if (last_processed_.time_since_epoch().count() != 0 &&
        now - last_processed_ < interval) {
        return false;
    }
    last_processed_ = now;
    return true;
}

void FrameWorker::produce_frame()
{
    if (!running_) return;
    try {
        QImage decoded;
        if (v4l2_) {
            V4L2FrameSource::Frame frame;
            const bool process_frame = preview_enabled_ && processing_due();
            if (!v4l2_->capture_one(frame, config_.poll_timeout_ms,
                                    false, process_frame)) {
                ++consecutive_timeouts_;
                emit status_changed(
                    QStringLiteral("Waiting for frame (%1/3)")
                        .arg(consecutive_timeouts_));
                if (consecutive_timeouts_ >= 3U) {
                    emit status_changed(QStringLiteral("Recovering stream"));
                    v4l2_.reset();
                    consecutive_timeouts_ = 0;
                    if (!open_v4l2_source()) {
                        if (timer_ != nullptr) timer_->stop();
                        running_ = false;
                    }
                }
                return;
            }
            consecutive_timeouts_ = 0;
            if (!process_frame) return;
            decoded = decode_v4l2_frame(frame);
            v4l2_->release(frame);
        } else {
            if (!preview_enabled_) return;
            if (!processing_due()) return;
            if (synthetic_) update_synthetic_frame();
            const int output_width = std::min(config_.width, config_.work_width);
            int output_height = static_cast<int>(
                static_cast<long long>(config_.height) * output_width /
                config_.width);
            output_height = std::max(2, output_height & ~1);
            decoded = Nv12Converter::to_rgb_scaled(
                nv12_, config_.width, config_.height,
                output_width, output_height);
        }
        auto image = std::make_shared<const QImage>(std::move(decoded));
        ++frame_count_;
        emit frame_ready(std::move(image));

        const auto now = std::chrono::steady_clock::now();
        const double elapsed =
            std::chrono::duration<double>(now - stats_started_).count();
        if (elapsed >= 1.0) {
            emit stats_ready(static_cast<double>(frame_count_) / elapsed,
                             frame_count_);
        }
    } catch (const std::exception& error) {
        shutdown_capture();
        emit failed(QString::fromUtf8(error.what()));
    }
}
