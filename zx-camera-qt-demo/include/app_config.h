#pragma once

#include <QString>

enum class SourceType {
    Synthetic,
    Nv12File,
    Ov13850,
    Uc325,
};

struct AppConfig {
    SourceType source{SourceType::Synthetic};
    QString input_path;
    QString device_path;
    QString pixel_format;
    int width{2112};
    int height{1568};
    int fps{30};
    int display_fps{15};
    int inference_fps{8};
    int work_width{960};
    int npu_workers{3};
    int poll_timeout_ms{1000};
    bool inference_enabled{false};
    QString model_path{QStringLiteral(
        "/home/zxwy/j7-yolov5/models/yolov5s_relu_rk3588_int8.rknn")};
};
