#pragma once

#include "detection_types.h"
#include "frame_types.h"

#include <QObject>
#include <QThread>

#include <cstdint>
#include <chrono>
#include <vector>

class InferenceWorker;

class InferenceScheduler final : public QObject {
    Q_OBJECT
public:
    InferenceScheduler(QString model_path, int worker_count,
                       int channel_count, QObject* parent = nullptr);
    ~InferenceScheduler() override;

    void submit(int channel, SharedImage image);
    void set_channel_enabled(int channel, bool enabled);
    void shutdown();

signals:
    void result_ready(int channel, SharedInferenceResult result);
    void status_changed(int channel, const QString& status);
    void failed(int channel, const QString& message);

private slots:
    void worker_ready(int worker_id);
    void worker_result(int worker_id, int channel,
                       SharedInferenceResult result);
    void worker_failed(int worker_id, int channel,
                       const QString& message);

private:
    void dispatch(int worker_id);
    int next_pending_channel();

    struct ChannelState {
        SharedImage pending;
        bool enabled{false};
        std::uint64_t submitted{};
        std::uint64_t dropped{};
        std::uint64_t completed{};
        std::chrono::steady_clock::time_point started{};
        bool first_result_logged{false};
    };
    QString model_path_;
    std::vector<QThread*> threads_;
    std::vector<InferenceWorker*> workers_;
    std::vector<bool> worker_idle_;
    std::vector<ChannelState> channels_;
    int round_robin_{};
    bool shutdown_{false};
};
