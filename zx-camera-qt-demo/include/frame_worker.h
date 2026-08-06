#pragma once

#include "app_config.h"
#include "frame_types.h"
#include "v4l2_frame_source.h"

#include <QObject>

#include <chrono>
#include <cstdint>
#include <vector>

class QTimer;

class FrameWorker final : public QObject {
    Q_OBJECT

public:
    explicit FrameWorker(AppConfig config, QObject* parent = nullptr);
    ~FrameWorker() override;

public slots:
    void start();
    void stop();
    void shutdown_capture();

signals:
    void frame_ready(SharedImage image);
    void stats_ready(double fps, std::uint64_t frames);
    void status_changed(const QString& status);
    void failed(const QString& message);

private slots:
    void produce_frame();

private:
    bool load_or_create_source();
    bool open_v4l2_source();
    void update_synthetic_frame();
    QImage decode_v4l2_frame(const V4L2FrameSource::Frame& frame) const;
    bool processing_due();

    AppConfig config_;
    QTimer* timer_{nullptr};
    std::unique_ptr<V4L2FrameSource> v4l2_;
    std::vector<std::uint8_t> nv12_;
    bool synthetic_{false};
    bool running_{false};
    bool preview_enabled_{false};
    std::uint64_t frame_count_{0};
    unsigned int consecutive_timeouts_{0};
    std::chrono::steady_clock::time_point stats_started_{};
    std::chrono::steady_clock::time_point last_processed_{};
};
