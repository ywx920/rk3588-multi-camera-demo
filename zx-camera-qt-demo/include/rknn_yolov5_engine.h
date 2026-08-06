#pragma once

#include "detection_types.h"

#include <QImage>
#include <QString>

#include <memory>

class RknnYolov5Engine final {
public:
    explicit RknnYolov5Engine(const QString& model_path);
    ~RknnYolov5Engine();

    RknnYolov5Engine(const RknnYolov5Engine&) = delete;
    RknnYolov5Engine& operator=(const RknnYolov5Engine&) = delete;

    InferenceResult infer(const QImage& image);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

