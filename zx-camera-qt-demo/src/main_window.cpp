#include "main_window.h"

#include "camera_panel.h"
#include "inference_scheduler.h"

#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

MainWindow::MainWindow(std::vector<CameraEntry> cameras, QWidget* parent)
    : QMainWindow(parent)
{
    const auto inference_camera = std::find_if(
        cameras.begin(), cameras.end(),
        [](const CameraEntry& entry) { return entry.config.inference_enabled; });
    if (inference_camera != cameras.end()) {
        scheduler_ = new InferenceScheduler(
            inference_camera->config.model_path,
            inference_camera->config.npu_workers,
            static_cast<int>(cameras.size()), this);
    }
    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);
    auto* scroll = new QScrollArea(central);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* card_container = new QWidget(scroll);
    auto* grid = new QGridLayout;
    grid->setContentsMargins(4, 4, 4, 4);
    grid->setSpacing(10);
    card_container->setLayout(grid);
    const QRect available = QGuiApplication::primaryScreen()
                                ? QGuiApplication::primaryScreen()->availableGeometry()
                                : QRect(0, 0, 1280, 720);
    constexpr int columns = 2;
    const int window_width = std::min(available.width(), 1500);
    const int content_width = std::max(320, window_width - 38);
    const int card_width = std::max(
        280, (content_width - (columns - 1) * grid->horizontalSpacing()) /
                 columns);
    const int viewport_width = std::clamp(card_width - 22, 260, 580);
    const int viewport_height = std::clamp(
        viewport_width * 9 / 16, 180, 330);
    const QSize viewport_size(viewport_width, viewport_height);
    for (std::size_t index = 0; index < cameras.size(); ++index) {
        auto* panel = new CameraPanel(
            static_cast<int>(index), cameras[index].title,
            std::move(cameras[index].config), scheduler_, viewport_size,
            card_container);
        panels_.push_back(panel);
        grid->addWidget(panel, static_cast<int>(index / static_cast<std::size_t>(columns)),
                        static_cast<int>(index % static_cast<std::size_t>(columns)));
    }
    if (cameras.size() < 4U) {
        auto* reserved_card = new QWidget(card_container);
        reserved_card->setObjectName(QStringLiteral("reservedCard"));
        reserved_card->setFixedSize(viewport_size.width() + 18,
                                    viewport_size.height() + 160);
        auto* reserved_layout = new QVBoxLayout(reserved_card);
        auto* reserved_title = new QLabel(QStringLiteral("预留通道"),
                                          reserved_card);
        reserved_title->setObjectName(QStringLiteral("cameraTitle"));
        auto* reserved_body = new QLabel(
            QStringLiteral("CAMERA SLOT 4\n可扩展 CSI / USB / RTSP 视频源"),
            reserved_card);
        reserved_body->setObjectName(QStringLiteral("reservedViewport"));
        reserved_body->setAlignment(Qt::AlignCenter);
        reserved_layout->addWidget(reserved_title);
        reserved_layout->addWidget(reserved_body, 1);
        grid->addWidget(reserved_card, 1, 1,
                        Qt::AlignTop | Qt::AlignHCenter);
    }
    scroll->setWidget(card_container);
    root->addWidget(scroll, 1);
    auto* controls = new QHBoxLayout;
    auto* start_all = new QPushButton(QStringLiteral("开启全部摄像头"), central);
    auto* stop_all = new QPushButton(QStringLiteral("关闭全部摄像头"), central);
    auto* inference_on = new QPushButton(QStringLiteral("开启全部推理"), central);
    auto* inference_off = new QPushButton(QStringLiteral("一键关闭推理"), central);
    start_all->setObjectName(QStringLiteral("startAll"));
    stop_all->setObjectName(QStringLiteral("stopAll"));
    inference_on->setObjectName(QStringLiteral("inferenceAllOn"));
    inference_off->setObjectName(QStringLiteral("inferenceAllOff"));
    start_all->setMinimumHeight(40);
    stop_all->setMinimumHeight(40);
    inference_on->setMinimumHeight(40);
    inference_off->setMinimumHeight(40);
    controls->addWidget(start_all);
    controls->addWidget(stop_all);
    controls->addWidget(inference_on);
    controls->addWidget(inference_off);
    controls->addStretch();
    root->addLayout(controls);
    connect(start_all, &QPushButton::clicked, this, &MainWindow::start_all);
    connect(stop_all, &QPushButton::clicked, this, [this] { for (auto* panel : panels_) panel->stop(); });
    connect(inference_on, &QPushButton::clicked, this,
            [this] { set_all_inference(true); });
    connect(inference_off, &QPushButton::clicked, this,
            [this] { set_all_inference(false); });
    setCentralWidget(central);
    setStyleSheet(QStringLiteral(R"(
        QMainWindow,QWidget { background:#0b1020; color:#e8edf7; }
        QWidget#cameraCard { background:#151c2e; border:1px solid #26324a;
          border-radius:14px; }
        QWidget#reservedCard { background:#101727; border:1px dashed #354562;
          border-radius:14px; }
        QLabel#cameraTitle { color:#f8fafc; font-size:17px; font-weight:700;
          padding:3px 4px; }
        QLabel#cameraViewport { background:#05070d; color:#74819a;
          border:1px solid #2a3853; border-radius:10px; }
        QLabel#reservedViewport { background:#0a1020; color:#5e6d86;
          border:1px dashed #33435f; border-radius:10px; font-size:15px; }
        QLabel#metric { background:#202a40; color:#78dce8; border-radius:7px;
          padding:5px 8px; font-family:monospace; }
        QLabel#cameraStatus { color:#9eacc3; padding-left:6px; }
        QPushButton { color:white; border:1px solid #5598ee;
          border-bottom:3px solid #164b91; border-radius:9px; padding:7px 15px;
          font-weight:700; background:qlineargradient(x1:0,y1:0,x2:0,y2:1,
          stop:0 #438cf1,stop:1 #2368cf); }
        QPushButton:hover { border-color:#8fc0ff;
          background:qlineargradient(x1:0,y1:0,x2:0,y2:1,
          stop:0 #5b9df7,stop:1 #2c75df); }
        QPushButton:pressed { border-bottom:1px solid #164b91; padding-top:9px;
          background:#185fc7; }
        QPushButton:disabled { background:#303a4d; color:#718096; }
        QPushButton#cameraToggle[active="true"],QPushButton#stopAll,
        QPushButton#inferenceAllOff {
          border-color:#ec7d88; border-bottom-color:#7f2430;
          background:qlineargradient(x1:0,y1:0,x2:0,y2:1,
          stop:0 #df6270,stop:1 #ac3443); }
        QPushButton#inferenceToggle[active="true"] {
          border-color:#d69cff; border-bottom-color:#642b91;
          background:qlineargradient(x1:0,y1:0,x2:0,y2:1,
          stop:0 #a960dc,stop:1 #7135aa); }
        QScrollArea,QScrollArea>QWidget>QWidget { background:#0b1020; border:0; }
        QScrollBar:vertical { background:#10182a; width:10px; margin:0; }
        QScrollBar::handle:vertical { background:#3c4e6d; border-radius:5px;
          min-height:32px; }
    )"));
    resize(window_width,
           std::min(available.height(), 920));
    move(available.topLeft());
    setWindowTitle(QStringLiteral("ZX Vision · Multi-Camera AI Console"));
}

MainWindow::~MainWindow()
{
    if (scheduler_) scheduler_->shutdown();
}

void MainWindow::start_all()
{
    for (auto* panel : panels_) panel->start();
}

void MainWindow::set_all_inference(bool enabled)
{
    for (auto* panel : panels_) panel->set_inference_active(enabled);
}
