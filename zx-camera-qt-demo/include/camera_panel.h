#pragma once

#include "app_config.h"
#include "detection_types.h"
#include "frame_types.h"

#include <QThread>
#include <QElapsedTimer>
#include <QSize>
#include <QWidget>

class FrameWorker;
class InferenceScheduler;
class QLabel;
class QPushButton;

class CameraPanel final : public QWidget {
    Q_OBJECT
public:
    CameraPanel(int channel, QString title, AppConfig config,
                InferenceScheduler* scheduler, QSize viewport_size,
                QWidget* parent = nullptr);
    ~CameraPanel() override;
    void start();
    void stop();
    void set_inference_active(bool enabled);
    bool inference_active() const { return inference_active_; }

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void show_frame(SharedImage image);
    void show_stats(double fps, std::uint64_t frames);
    void show_status(const QString& status);
    void show_error(const QString& message);
    void show_inference(SharedInferenceResult result);
    void show_inference_status(const QString& status);
    void show_inference_error(const QString& message);

private:
    void refresh();
    void shutdown();
    void update_control_buttons();
    SharedInferenceResult smooth_inference(SharedInferenceResult result);
    QThread thread_;
    FrameWorker* worker_{nullptr};
    InferenceScheduler* scheduler_{nullptr};
    int channel_{};
    QLabel* image_{nullptr};
    QLabel* stats_{nullptr};
    QLabel* status_{nullptr};
    QLabel* inference_stats_{nullptr};
    QPushButton* camera_toggle_{nullptr};
    QPushButton* inference_toggle_{nullptr};
    SharedImage current_;
    SharedInferenceResult inference_result_;
    bool inference_enabled_{false};
    bool inference_active_{false};
    bool active_{false};
    int display_interval_ms_{66};
    int inference_interval_ms_{125};
    int empty_detection_streak_{0};
    int next_track_id_{1};
    QElapsedTimer display_clock_;
    QElapsedTimer inference_clock_;
    QElapsedTimer inference_result_clock_;
    bool shutdown_{false};
};
