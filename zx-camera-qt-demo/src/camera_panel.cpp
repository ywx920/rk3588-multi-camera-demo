#include "camera_panel.h"

#include "frame_worker.h"
#include "inference_scheduler.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>

CameraPanel::CameraPanel(int channel, QString title, AppConfig config,
                         InferenceScheduler* scheduler, QSize viewport_size,
                         QWidget* parent)
    : QWidget(parent), scheduler_(scheduler), channel_(channel)
{
    setObjectName(QStringLiteral("cameraCard"));
    inference_enabled_ = config.inference_enabled;
    inference_active_ = inference_enabled_;
    display_interval_ms_ = std::max(1, 1000 / config.display_fps);
    inference_interval_ms_ = std::max(1, 1000 / config.inference_fps);
    auto* layout = new QVBoxLayout(this);
    auto* heading = new QLabel(std::move(title), this);
    heading->setObjectName(QStringLiteral("cameraTitle"));
    heading->setStyleSheet(QStringLiteral("font-weight:bold;font-size:16px"));
    image_ = new QLabel(QStringLiteral("摄像头已关闭"), this);
    image_->setAlignment(Qt::AlignCenter);
    image_->setFixedSize(viewport_size);
    image_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    image_->setObjectName(QStringLiteral("cameraViewport"));
    layout->addWidget(heading);
    layout->addWidget(image_, 1);
    auto* controls = new QHBoxLayout;
    camera_toggle_ = new QPushButton(this);
    camera_toggle_->setObjectName(QStringLiteral("cameraToggle"));
    camera_toggle_->setMinimumHeight(38);
    inference_toggle_ = new QPushButton(this);
    inference_toggle_->setObjectName(QStringLiteral("inferenceToggle"));
    inference_toggle_->setMinimumHeight(38);
    inference_toggle_->setEnabled(inference_enabled_);
    stats_ = new QLabel(QStringLiteral("0.0 FPS"), this);
    stats_->setObjectName(QStringLiteral("metric"));
    stats_->setAlignment(Qt::AlignCenter);
    stats_->setFixedHeight(28);
    status_ = new QLabel(QStringLiteral("Ready"), this);
    status_->setObjectName(QStringLiteral("cameraStatus"));
    status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    status_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    status_->setFixedHeight(26);
    inference_stats_ = new QLabel(
        inference_enabled_ ? QStringLiteral("NPU: loading")
                           : QStringLiteral("NPU: off"), this);
    inference_stats_->setObjectName(QStringLiteral("metric"));
    inference_stats_->setSizePolicy(QSizePolicy::Ignored,
                                    QSizePolicy::Preferred);
    inference_stats_->setMinimumWidth(0);
    inference_stats_->setAlignment(Qt::AlignCenter);
    inference_stats_->setFixedHeight(28);
    controls->addWidget(camera_toggle_, 1);
    controls->addWidget(inference_toggle_, 1);
    layout->addLayout(controls);
    auto* information = new QHBoxLayout;
    information->addWidget(stats_, 1);
    information->addWidget(inference_stats_, 1);
    layout->addLayout(information);
    layout->addWidget(status_);
    setFixedWidth(viewport_size.width() + layout->contentsMargins().left() +
                  layout->contentsMargins().right());
    setFixedHeight(viewport_size.height() + 160);
    update_control_buttons();

    worker_ = new FrameWorker(std::move(config));
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(camera_toggle_, &QPushButton::clicked, this,
            [this] { active_ ? stop() : start(); });
    connect(worker_, &FrameWorker::frame_ready, this, &CameraPanel::show_frame, Qt::QueuedConnection);
    connect(worker_, &FrameWorker::stats_ready, this, &CameraPanel::show_stats, Qt::QueuedConnection);
    connect(worker_, &FrameWorker::status_changed, this, &CameraPanel::show_status, Qt::QueuedConnection);
    connect(worker_, &FrameWorker::failed, this, &CameraPanel::show_error, Qt::QueuedConnection);
    thread_.start();

    if (inference_enabled_ && scheduler_) {
        connect(scheduler_, &InferenceScheduler::result_ready, this,
                [this](int channel_id, SharedInferenceResult result) {
                    if (channel_id == channel_) show_inference(std::move(result));
                });
        connect(scheduler_, &InferenceScheduler::status_changed, this,
                [this](int channel_id, const QString& text) {
                    if (channel_id == channel_) show_inference_status(text);
                });
        connect(scheduler_, &InferenceScheduler::failed, this,
                [this](int channel_id, const QString& text) {
                    if (channel_id == channel_) show_inference_error(text);
                });
    }
    connect(inference_toggle_, &QPushButton::clicked, this,
            [this] { set_inference_active(!inference_active_); });
}

CameraPanel::~CameraPanel() { shutdown(); }

void CameraPanel::start()
{
    if (!worker_) return;
    active_ = true;
    update_control_buttons();
    display_clock_.restart();
    inference_clock_.restart();
    inference_result_clock_.invalidate();
    empty_detection_streak_ = 0;
    if (scheduler_) scheduler_->set_channel_enabled(
        channel_, inference_active_);
    QMetaObject::invokeMethod(worker_, "start", Qt::QueuedConnection);
}

void CameraPanel::stop()
{
    if (!worker_) return;
    QMetaObject::invokeMethod(worker_, "stop", Qt::QueuedConnection);
    active_ = false;
    if (scheduler_) scheduler_->set_channel_enabled(channel_, false);
    current_.reset();
    inference_result_.reset();
    inference_result_clock_.invalidate();
    empty_detection_streak_ = 0;
    image_->setPixmap(QPixmap());
    image_->setText(QStringLiteral("摄像头已关闭"));
    stats_->setText(QStringLiteral("0.0 FPS"));
    inference_stats_->setText(inference_active_
        ? QStringLiteral("NPU: standby") : QStringLiteral("NPU: off"));
    status_->setText(QStringLiteral("摄像头已关闭"));
    update_control_buttons();
}

void CameraPanel::set_inference_active(bool enabled)
{
    if (!inference_enabled_) return;
    inference_active_ = enabled;
    inference_result_.reset();
    inference_result_clock_.invalidate();
    empty_detection_streak_ = 0;
    next_track_id_ = 1;
    if (scheduler_)
        scheduler_->set_channel_enabled(channel_, active_ && enabled);
    inference_stats_->setText(enabled ? QStringLiteral("NPU: scheduled")
                                      : QStringLiteral("NPU: off"));
    update_control_buttons();
    refresh();
}

void CameraPanel::show_frame(SharedImage image)
{
    if (!active_) return;
    current_ = std::move(image);
    if (inference_result_ && inference_result_clock_.isValid() &&
        inference_result_clock_.elapsed() >
            std::max(2000, inference_interval_ms_ * 8)) {
        inference_result_.reset();
        inference_stats_->setText(QStringLiteral("NPU: waiting result"));
    }
    int inference_multiplier = 1;
    if (empty_detection_streak_ >= 30) inference_multiplier = 4;
    else if (empty_detection_streak_ >= 10) inference_multiplier = 3;
    else if (empty_detection_streak_ >= 3) inference_multiplier = 2;
    if (scheduler_ && inference_active_ &&
        (!inference_clock_.isValid() ||
         inference_clock_.elapsed() >=
             std::min(1500, inference_interval_ms_ *
                 inference_multiplier))) {
        inference_clock_.restart();
        scheduler_->submit(channel_, current_);
    }
    if (!display_clock_.isValid() ||
        display_clock_.elapsed() >= display_interval_ms_) {
        display_clock_.restart();
        refresh();
    }
}
void CameraPanel::show_stats(double fps, std::uint64_t frames)
{
    if (!active_) return;
    stats_->setText(QStringLiteral("%1 FPS · %2").arg(fps, 0, 'f', 1).arg(static_cast<qulonglong>(frames)));
}
void CameraPanel::show_status(const QString& text)
{
    status_->setText(active_ ? text : QStringLiteral("摄像头已关闭"));
}
void CameraPanel::show_error(const QString& text)
{
    active_ = false;
    current_.reset();
    inference_result_.reset();
    image_->setPixmap(QPixmap());
    image_->setText(QStringLiteral("摄像头异常"));
    update_control_buttons();
    status_->setText(text);
}
void CameraPanel::show_inference(SharedInferenceResult result)
{
    if (!active_ || !inference_active_) return;
    inference_result_ = smooth_inference(std::move(result));
    inference_result_clock_.restart();
    if (inference_result_) {
        empty_detection_streak_ = inference_result_->detections.empty()
            ? empty_detection_streak_ + 1 : 0;
        inference_stats_->setText(
            QStringLiteral("NPU %1ms · %2FPS · %3 obj")
                .arg(inference_result_->inference_ms, 0, 'f', 1)
                .arg(inference_result_->channel_fps, 0, 'f', 1)
                .arg(inference_result_->detections.size()));
        inference_stats_->setToolTip(
            QStringLiteral("NPU: %1 ms · actual %2 FPS · drop %3 · %4 obj")
                .arg(inference_result_->inference_ms, 0, 'f', 1)
                .arg(inference_result_->channel_fps, 0, 'f', 1)
                .arg(static_cast<qulonglong>(
                    inference_result_->dropped_frames))
                .arg(inference_result_->detections.size()));
    }
}
void CameraPanel::show_inference_status(const QString& text)
{
    if (!active_) return;
    inference_stats_->setText(QStringLiteral("NPU: %1").arg(text));
}
void CameraPanel::show_inference_error(const QString& text)
{
    if (!active_) return;
    inference_stats_->setText(QStringLiteral("NPU error: %1").arg(text));
}
void CameraPanel::refresh()
{
    if (!current_ || current_->isNull()) return;
    QImage rendered = inference_result_ ? current_->copy() : *current_;
    if (inference_result_) {
        QPainter painter(&rendered);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const qreal line_width = std::max<qreal>(2.0, rendered.width() / 700.0);
        painter.setPen(QPen(QColor(0, 255, 80), line_width));
        QFont font = painter.font();
        font.setPixelSize(std::max(18, rendered.width() / 70));
        font.setBold(true);
        painter.setFont(font);
        for (const auto& detection : inference_result_->detections) {
            painter.drawRect(detection.box);
            const QString label = QStringLiteral("%1 #%2 %3")
                .arg(detection.label).arg(detection.track_id)
                .arg(detection.confidence, 0, 'f', 2);
            const QRectF text_box = painter.boundingRect(
                QRectF(), Qt::AlignLeft, label).adjusted(-4, -2, 4, 2);
            const QPointF origin(detection.box.left(),
                                 std::max(0.0, detection.box.top() - text_box.height()));
            QRectF background(origin, text_box.size());
            painter.fillRect(background, QColor(0, 0, 0, 170));
            painter.drawText(background.adjusted(4, 2, -4, -2),
                             Qt::AlignLeft | Qt::AlignVCenter, label);
        }
    }
    image_->setPixmap(QPixmap::fromImage(rendered).scaled(
        image_->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
}
SharedInferenceResult CameraPanel::smooth_inference(
    SharedInferenceResult result)
{
    if (!result) return result;
    auto smoothed = std::make_shared<InferenceResult>(*result);
    if (!inference_result_) {
        for (auto& detection : smoothed->detections)
            detection.track_id = next_track_id_++;
        return smoothed;
    }
    constexpr qreal previous_weight = 0.35;
    for (auto& current : smoothed->detections) {
        const Detection* best = nullptr;
        qreal best_iou = 0.0;
        for (const auto& previous : inference_result_->detections) {
            if (previous.label != current.label) continue;
            const QRectF intersection =
                previous.box.intersected(current.box);
            const qreal union_area = previous.box.width() * previous.box.height() +
                current.box.width() * current.box.height() -
                intersection.width() * intersection.height();
            const qreal iou = union_area > 0.0
                ? intersection.width() * intersection.height() / union_area
                : 0.0;
            if (iou > best_iou) {
                best_iou = iou;
                best = &previous;
            }
        }
        if (best && best_iou >= 0.25) {
            current.track_id = best->track_id;
            const QRectF& old = best->box;
            const QRectF& fresh = current.box;
            current.box = QRectF(
                old.x() * previous_weight + fresh.x() * (1.0 - previous_weight),
                old.y() * previous_weight + fresh.y() * (1.0 - previous_weight),
                old.width() * previous_weight + fresh.width() * (1.0 - previous_weight),
                old.height() * previous_weight + fresh.height() * (1.0 - previous_weight));
        } else current.track_id = next_track_id_++;
    }
    return smoothed;
}
void CameraPanel::shutdown()
{
    if (shutdown_) return;
    shutdown_ = true;
    if (worker_ && thread_.isRunning()) {
        QMetaObject::invokeMethod(worker_, "shutdown_capture", Qt::BlockingQueuedConnection);
        thread_.quit();
        thread_.wait();
    }
    worker_ = nullptr;
    scheduler_ = nullptr;
}
void CameraPanel::update_control_buttons()
{
    camera_toggle_->setText(active_ ? QStringLiteral("关闭摄像头")
                                    : QStringLiteral("开启摄像头"));
    camera_toggle_->setProperty("active", active_);
    inference_toggle_->setText(
        inference_active_ ? QStringLiteral("关闭推理")
                          : QStringLiteral("开启推理"));
    inference_toggle_->setProperty("active", inference_active_);
    for (auto* button : {camera_toggle_, inference_toggle_}) {
        button->style()->unpolish(button);
        button->style()->polish(button);
        button->update();
    }
}
void CameraPanel::resizeEvent(QResizeEvent* event) { QWidget::resizeEvent(event); refresh(); }
