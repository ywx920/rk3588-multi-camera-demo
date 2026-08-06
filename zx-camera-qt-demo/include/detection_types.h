#pragma once

#include <QMetaType>
#include <QRectF>
#include <QString>

#include <memory>
#include <cstdint>
#include <vector>

struct Detection {
    QRectF box;
    QString label;
    float confidence{};
    int track_id{};
};

struct InferenceResult {
    std::vector<Detection> detections;
    double inference_ms{};
    double inference_fps{};
    double channel_fps{};
    std::uint64_t dropped_frames{};
};

using SharedInferenceResult = std::shared_ptr<const InferenceResult>;
Q_DECLARE_METATYPE(SharedInferenceResult)
