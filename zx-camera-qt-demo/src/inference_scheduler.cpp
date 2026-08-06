#include "inference_scheduler.h"

#include "inference_worker.h"

#include <QMetaObject>
#include <QDebug>

#include <algorithm>
#include <utility>

InferenceScheduler::InferenceScheduler(QString model_path, int worker_count,
                                       int channel_count, QObject* parent)
    : QObject(parent), model_path_(std::move(model_path)),
      channels_(static_cast<std::size_t>(std::max(0, channel_count)))
{
    const int count = std::clamp(worker_count, 1,
                                 std::max(1, channel_count));
    threads_.reserve(static_cast<std::size_t>(count));
    workers_.reserve(static_cast<std::size_t>(count));
    worker_idle_.resize(static_cast<std::size_t>(count), false);
    for (int id = 0; id < count; ++id) {
        auto* thread = new QThread(this);
        auto* worker = new InferenceWorker(id, model_path_);
        worker->moveToThread(thread);
        connect(thread, &QThread::finished, worker, &QObject::deleteLater);
        connect(worker, &InferenceWorker::ready,
                this, &InferenceScheduler::worker_ready,
                Qt::QueuedConnection);
        connect(worker, &InferenceWorker::result_ready,
                this, &InferenceScheduler::worker_result,
                Qt::QueuedConnection);
        connect(worker, &InferenceWorker::failed,
                this, &InferenceScheduler::worker_failed,
                Qt::QueuedConnection);
        threads_.push_back(thread);
        workers_.push_back(worker);
        thread->start();
        QMetaObject::invokeMethod(worker, "initialize", Qt::QueuedConnection);
    }
    qInfo() << "Inference scheduler:" << count << "worker(s),"
            << channel_count << "channel(s)";
}

InferenceScheduler::~InferenceScheduler()
{
    shutdown();
}

void InferenceScheduler::submit(int channel, SharedImage image)
{
    if (shutdown_ || channel < 0 ||
        channel >= static_cast<int>(channels_.size()) || !image) return;
    auto& state = channels_[static_cast<std::size_t>(channel)];
    if (!state.enabled) return;
    ++state.submitted;
    if (state.pending) ++state.dropped;
    state.pending = std::move(image);
    for (int worker = 0; worker < static_cast<int>(worker_idle_.size());
         ++worker) {
        if (worker_idle_[static_cast<std::size_t>(worker)]) dispatch(worker);
    }
}

void InferenceScheduler::set_channel_enabled(int channel, bool enabled)
{
    if (channel < 0 || channel >= static_cast<int>(channels_.size())) return;
    auto& state = channels_[static_cast<std::size_t>(channel)];
    state.enabled = enabled;
    if (!enabled) state.pending.reset();
    else if (state.started.time_since_epoch().count() == 0)
        state.started = std::chrono::steady_clock::now();
    emit status_changed(channel, enabled ? QStringLiteral("scheduled")
                                         : QStringLiteral("paused"));
}

void InferenceScheduler::worker_ready(int worker_id)
{
    if (shutdown_ || worker_id < 0 ||
        worker_id >= static_cast<int>(worker_idle_.size())) return;
    worker_idle_[static_cast<std::size_t>(worker_id)] = true;
    dispatch(worker_id);
}

void InferenceScheduler::worker_result(int worker_id, int channel,
                                       SharedInferenceResult result)
{
    if (channel >= 0 && channel < static_cast<int>(channels_.size()) && result) {
        auto& state = channels_[static_cast<std::size_t>(channel)];
        ++state.completed;
        const double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - state.started).count();
        auto enriched = std::make_shared<InferenceResult>(*result);
        enriched->channel_fps = elapsed > 0.0
            ? static_cast<double>(state.completed) / elapsed : 0.0;
        enriched->dropped_frames = state.dropped;
        if (!state.first_result_logged) {
            qInfo() << "Inference channel" << channel
                    << "first result from worker" << worker_id;
            state.first_result_logged = true;
        }
        emit result_ready(channel, std::move(enriched));
    }
    worker_ready(worker_id);
}

void InferenceScheduler::worker_failed(int worker_id, int channel,
                                       const QString& message)
{
    if (channel >= 0) emit failed(channel, message);
    else {
        for (int index = 0; index < static_cast<int>(channels_.size()); ++index)
            emit failed(index, message);
    }
    worker_ready(worker_id);
}

int InferenceScheduler::next_pending_channel()
{
    const int count = static_cast<int>(channels_.size());
    for (int offset = 0; offset < count; ++offset) {
        const int channel = (round_robin_ + offset) % count;
        const auto& state = channels_[static_cast<std::size_t>(channel)];
        if (state.enabled && state.pending) {
            round_robin_ = (channel + 1) % count;
            return channel;
        }
    }
    return -1;
}

void InferenceScheduler::dispatch(int worker_id)
{
    if (shutdown_ || !worker_idle_[static_cast<std::size_t>(worker_id)]) return;
    const int channel = next_pending_channel();
    if (channel < 0) return;
    auto& state = channels_[static_cast<std::size_t>(channel)];
    SharedImage image = std::move(state.pending);
    worker_idle_[static_cast<std::size_t>(worker_id)] = false;
    QMetaObject::invokeMethod(
        workers_[static_cast<std::size_t>(worker_id)], "process",
        Qt::QueuedConnection, Q_ARG(int, channel),
        Q_ARG(SharedImage, std::move(image)));
}

void InferenceScheduler::shutdown()
{
    if (shutdown_) return;
    shutdown_ = true;
    for (auto& channel : channels_) channel.pending.reset();
    for (std::size_t index = 0; index < workers_.size(); ++index) {
        if (threads_[index]->isRunning()) {
            QMetaObject::invokeMethod(workers_[index], "shutdown",
                                      Qt::BlockingQueuedConnection);
            threads_[index]->quit();
            threads_[index]->wait();
        }
    }
    workers_.clear();
}
