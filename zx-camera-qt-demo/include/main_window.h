#pragma once

#include "app_config.h"

#include <QMainWindow>
#include <vector>

class CameraPanel;
class InferenceScheduler;

struct CameraEntry {
    QString title;
    AppConfig config;
};

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(std::vector<CameraEntry> cameras, QWidget* parent = nullptr);
    ~MainWindow() override;
    void start_all();
private:
    void set_all_inference(bool enabled);
    std::vector<CameraPanel*> panels_;
    InferenceScheduler* scheduler_{nullptr};
};
