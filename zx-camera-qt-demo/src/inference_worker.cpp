#include "inference_worker.h"

#include "rknn_yolov5_engine.h"

#include <QDebug>

#include <exception>
#include <utility>

InferenceWorker::InferenceWorker(int worker_id, QString model_path,
                                 QObject* parent)
    : QObject(parent), worker_id_(worker_id),
      model_path_(std::move(model_path)) {}

InferenceWorker::~InferenceWorker() = default;

void InferenceWorker::initialize()
{
    try {
        engine_ = std::make_unique<RknnYolov5Engine>(model_path_);
        qInfo().noquote() << "RKNN worker" << worker_id_
                          << "initialized:" << model_path_;
        emit ready(worker_id_);
    } catch (const std::exception& error) {
        emit failed(worker_id_, -1, QString::fromUtf8(error.what()));
    }
}

void InferenceWorker::process(int channel, SharedImage image)
{
    if (!engine_ || !image || image->isNull()) {
        emit ready(worker_id_);
        return;
    }
    try {
        auto result = std::make_shared<const InferenceResult>(
            engine_->infer(*image));
        if (!first_result_logged_) {
            qInfo() << "RKNN worker" << worker_id_
                    << "first frame complete:"
                    << result->inference_ms << "ms, channel" << channel;
            first_result_logged_ = true;
        }
        emit result_ready(worker_id_, channel, std::move(result));
    } catch (const std::exception& error) {
        emit failed(worker_id_, channel, QString::fromUtf8(error.what()));
    }
}

void InferenceWorker::shutdown()
{
    engine_.reset();
}
