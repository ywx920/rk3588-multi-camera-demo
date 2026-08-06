#pragma once

#include "detection_types.h"
#include "frame_types.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <memory>
#include <mutex>

class RknnYolov5Engine;

class InferenceWorker final : public QObject {
    Q_OBJECT
public:
    InferenceWorker(int worker_id, QString model_path,
                    QObject* parent = nullptr);
    ~InferenceWorker() override;

public slots:
    void initialize();
    void process(int channel, SharedImage image);
    void shutdown();

signals:
    void ready(int worker_id);
    void result_ready(int worker_id, int channel,
                      SharedInferenceResult result);
    void failed(int worker_id, int channel, const QString& message);

private:
    int worker_id_{};
    QString model_path_;
    std::unique_ptr<RknnYolov5Engine> engine_;
    bool first_result_logged_{false};
};
