#include "rknn_yolov5_engine.h"

#include "rknn_demo_utils.h"
#include "yolo.h"

#include <QElapsedTimer>
#include <QDir>
#include <QFileInfo>
#include <QPainter>

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>

class RknnYolov5Engine::Impl {
public:
    explicit Impl(const QString& model_path)
        : model_path_(model_path.toLocal8Bit())
    {
        if (model_path_.isEmpty()) throw std::runtime_error("empty RKNN model path");

        std::memset(&model_, 0, sizeof(model_));
        std::memset(&yolo_, 0, sizeof(yolo_));
        model_.m_path = model_path_.data();
        yolo_.m_type = YOLOV5;
        yolo_.post_type = Q8;
        yolo_.anchor_per_branch = 3;
        constexpr std::array<int, 18> anchors{
            10, 13, 16, 30, 33, 23,
            30, 61, 62, 45, 59, 119,
            116, 90, 156, 198, 373, 326};
        std::copy(anchors.begin(), anchors.end(), yolo_.anchors);
        QDir app_dir = QFileInfo(model_path).absoluteDir();
        app_dir.cdUp();
        const QByteArray labels = app_dir.filePath(
            QStringLiteral("model/coco_80_labels_list.txt")).toLocal8Bit();
        set_label_path(labels.constData());

        if (rkdemo_init(&model_) != 0) throw std::runtime_error("rkdemo_init failed");
        initialized_ = true;
        if (rkdemo_init_input_buffer_all(&model_, NORMAL_API,
                                         RKNN_TENSOR_UINT8) != 0) {
            throw std::runtime_error("RKNN input initialization failed");
        }
        if (rkdemo_init_output_buffer_all(&model_, NORMAL_API, 0) != 0) {
            throw std::runtime_error("RKNN output initialization failed");
        }
        if (model_.n_input != 1 || model_.n_output < 3) {
            throw std::runtime_error("unexpected YOLOv5 tensor count");
        }
        input_height_ = static_cast<int>(model_.in_attr[0].dims[1]);
        input_width_ = static_cast<int>(model_.in_attr[0].dims[2]);
        if (input_width_ <= 0 || input_height_ <= 0) {
            throw std::runtime_error("invalid RKNN input dimensions");
        }
    }

    ~Impl()
    {
        if (initialized_) rkdemo_release(&model_);
    }

    InferenceResult infer(const QImage& source)
    {
        if (source.isNull()) throw std::runtime_error("empty inference image");
        const QImage rgb = source.convertToFormat(QImage::Format_RGB888);
        const double scale = std::min(
            static_cast<double>(input_width_) / rgb.width(),
            static_cast<double>(input_height_) / rgb.height());
        const int resized_width = std::max(1, static_cast<int>(rgb.width() * scale));
        const int resized_height = std::max(1, static_cast<int>(rgb.height() * scale));
        const int pad_x = (input_width_ - resized_width) / 2;
        const int pad_y = (input_height_ - resized_height) / 2;

        QImage input(input_width_, input_height_, QImage::Format_RGB888);
        input.fill(QColor(114, 114, 114));
        {
            QPainter painter(&input);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.drawImage(QRect(pad_x, pad_y, resized_width, resized_height), rgb);
        }

        model_.inputs[0].buf = input.bits();
        model_.inputs[0].size = static_cast<uint32_t>(input.sizeInBytes());
        QElapsedTimer timer;
        timer.start();
        int ret = rknn_inputs_set(model_.ctx, model_.n_input, model_.inputs);
        if (ret != RKNN_SUCC) throw std::runtime_error("rknn_inputs_set failed");
        ret = rknn_run(model_.ctx, nullptr);
        if (ret != RKNN_SUCC) throw std::runtime_error("rknn_run failed");
        ret = rknn_outputs_get(model_.ctx, model_.n_output, model_.outputs, nullptr);
        if (ret != RKNN_SUCC) throw std::runtime_error("rknn_outputs_get failed");

        std::vector<void*> outputs(static_cast<std::size_t>(model_.n_output));
        for (int index = 0; index < model_.n_output; ++index)
            outputs[static_cast<std::size_t>(index)] = model_.outputs[index].buf;
        detect_result_group_t group{};
        ret = post_process(outputs.data(), &model_, &yolo_, &group);
        const double elapsed_ms = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
        const int release_ret = rknn_outputs_release(
            model_.ctx, model_.n_output, model_.outputs);
        if (ret != 0) throw std::runtime_error("YOLOv5 post process failed");
        if (release_ret != RKNN_SUCC)
            throw std::runtime_error("rknn_outputs_release failed");

        InferenceResult result;
        result.inference_ms = elapsed_ms;
        result.inference_fps = elapsed_ms > 0.0 ? 1000.0 / elapsed_ms : 0.0;
        result.detections.reserve(static_cast<std::size_t>(group.count));
        for (int index = 0; index < group.count; ++index) {
            const auto& item = group.results[index];
            const double left = std::clamp((item.box.left - pad_x) / scale,
                                           0.0, static_cast<double>(rgb.width() - 1));
            const double top = std::clamp((item.box.top - pad_y) / scale,
                                          0.0, static_cast<double>(rgb.height() - 1));
            const double right = std::clamp((item.box.right - pad_x) / scale,
                                            0.0, static_cast<double>(rgb.width() - 1));
            const double bottom = std::clamp((item.box.bottom - pad_y) / scale,
                                             0.0, static_cast<double>(rgb.height() - 1));
            if (right <= left || bottom <= top) continue;
            result.detections.push_back({
                QRectF(QPointF(left, top), QPointF(right, bottom)),
                QString::fromUtf8(item.name), item.prop});
        }
        return result;
    }

private:
    QByteArray model_path_;
    MODEL_INFO model_{};
    YOLO_INFO yolo_{};
    int input_width_{};
    int input_height_{};
    bool initialized_{false};
};

RknnYolov5Engine::RknnYolov5Engine(const QString& model_path)
    : impl_(std::make_unique<Impl>(model_path)) {}

RknnYolov5Engine::~RknnYolov5Engine() = default;

InferenceResult RknnYolov5Engine::infer(const QImage& image)
{
    return impl_->infer(image);
}
