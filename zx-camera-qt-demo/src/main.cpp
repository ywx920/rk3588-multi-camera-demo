#include "app_config.h"
#include "frame_types.h"
#include "detection_types.h"
#include "main_window.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <vector>

namespace {

QString find_uc325()
{
    QDir sysfs(QStringLiteral("/sys/class/video4linux"));
    const auto nodes = sysfs.entryList({QStringLiteral("video*")}, QDir::Dirs,
                                       QDir::Name);
    for (const QString& node : nodes) {
        QFile name(sysfs.filePath(node + QStringLiteral("/name")));
        if (name.open(QIODevice::ReadOnly) && name.readAll().contains("UC325"))
            return QStringLiteral("/dev/") + node;
    }
    return QStringLiteral("/dev/video-uc325");
}

bool parse_positive_even(const QString& text, int& value)
{
    bool ok = false;
    const int parsed = text.toInt(&ok);
    if (!ok || parsed <= 0 || (parsed % 2) != 0) return false;
    value = parsed;
    return true;
}

bool parse_source(const QString& text, AppConfig& config)
{
    if (text == QStringLiteral("all")) {
        config.source = SourceType::Synthetic;
        config.pixel_format = QStringLiteral("NV12");
        return true;
    }
    if (text == QStringLiteral("synthetic")) {
        config.source = SourceType::Synthetic;
        config.pixel_format = QStringLiteral("NV12");
        return true;
    }
    if (text == QStringLiteral("nv12")) {
        config.source = SourceType::Nv12File;
        config.pixel_format = QStringLiteral("NV12");
        return true;
    }
    if (text == QStringLiteral("ov13850")) {
        config.source = SourceType::Ov13850;
        config.device_path = QStringLiteral("/dev/video-camera0");
        config.pixel_format = QStringLiteral("NV12");
        return true;
    }
    if (text == QStringLiteral("ov13850-j8")) {
        config.source = SourceType::Ov13850;
        config.device_path = QStringLiteral("/dev/video-camera1");
        config.pixel_format = QStringLiteral("NV12");
        return true;
    }
    if (text == QStringLiteral("uc325")) {
        config.source = SourceType::Uc325;
        config.device_path = find_uc325();
        config.pixel_format = QStringLiteral("MJPG");
        return true;
    }
    return false;
}

std::vector<CameraEntry> load_camera_config(const QString& path,
                                            const AppConfig& defaults)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error(
            QStringLiteral("Cannot open camera config: %1").arg(path)
                .toStdString());
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        throw std::runtime_error(
            QStringLiteral("Invalid camera JSON: %1").arg(parse_error.errorString())
                .toStdString());
    std::vector<CameraEntry> cameras;
    for (const auto value : document.object().value("cameras").toArray()) {
        const auto object = value.toObject();
        if (!object.value("enabled").toBool(true)) continue;
        AppConfig config = defaults;
        const QString type = object.value("type").toString();
        if (!parse_source(type, config) || type == QStringLiteral("all"))
            throw std::runtime_error("invalid camera type in JSON");
        config.device_path = object.value("device").toString(config.device_path);
        config.pixel_format = object.value("format").toString(config.pixel_format).toUpper();
        config.width = object.value("width").toInt(config.width);
        config.height = object.value("height").toInt(config.height);
        config.fps = object.value("fps").toInt(config.fps);
        config.display_fps = object.value("display_fps").toInt(defaults.display_fps);
        config.inference_fps = object.value("inference_fps").toInt(defaults.inference_fps);
        config.work_width = object.value("work_width").toInt(defaults.work_width);
        config.inference_enabled = object.value("inference").toBool(defaults.inference_enabled);
        cameras.push_back({object.value("title").toString(
                               QStringLiteral("Camera %1").arg(cameras.size() + 1U)),
                           std::move(config)});
    }
    if (cameras.empty()) throw std::runtime_error("camera JSON has no enabled cameras");
    return cameras;
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("zx-camera-qt-demo"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.0"));
    qRegisterMetaType<SharedImage>("SharedImage");
    qRegisterMetaType<SharedInferenceResult>("SharedInferenceResult");

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Minimal Qt5 V4L2 camera preview demo"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({{"s", "source"},
                      "Source: all, synthetic, nv12, ov13850, ov13850-j8, uc325",
                      "name", "all"});
    parser.addOption({{"i", "input"}, "Single NV12 frame file", "path"});
    parser.addOption({{"d", "device"}, "Override V4L2 device path", "path"});
    parser.addOption({{"p", "format"}, "Override V4L2 FOURCC", "fourcc"});
    parser.addOption({{"W", "width"}, "Frame width", "pixels", "2112"});
    parser.addOption({{"H", "height"}, "Frame height", "pixels", "1568"});
    parser.addOption({{"f", "fps"}, "Preview rate", "fps", "30"});
    parser.addOption({"display-fps", "Maximum UI refresh rate", "fps", "15"});
    parser.addOption({"inference-fps", "Maximum inference submission rate", "fps", "8"});
    parser.addOption({"work-width", "RGB processing width (aspect ratio is preserved)",
                      "pixels", "960"});
    parser.addOption({"npu-workers", "Shared RKNN worker/context count", "count", "3"});
    parser.addOption({"config", "Camera JSON used by --source all", "path",
                      "/home/zxwy/zx-camera-qt-demo/config/cameras.json"});
    parser.addOption({"inference", "Enable YOLOv5 RKNN inference"});
    parser.addOption({"model", "RKNN model path", "path",
                      "/home/zxwy/j7-yolov5/models/yolov5s_relu_rk3588_int8.rknn"});
    parser.addOption({"auto-start", "Start capture automatically"});
    parser.process(app);

    AppConfig config;
    if (!parse_source(parser.value("source"), config)) {
        QMessageBox::critical(nullptr, QStringLiteral("Invalid arguments"),
                              QStringLiteral("Unknown source type"));
        return 2;
    }
    config.input_path = parser.value("input");
    config.inference_enabled = parser.isSet("inference");
    config.model_path = parser.value("model");
    if (!config.input_path.isEmpty() && !parser.isSet("source")) {
        config.source = SourceType::Nv12File;
        config.pixel_format = QStringLiteral("NV12");
    }
    if (!parser.value("device").isEmpty()) {
        config.device_path = parser.value("device");
    }
    if (!parser.value("format").isEmpty()) {
        config.pixel_format = parser.value("format").toUpper();
    }
    if (config.source == SourceType::Nv12File &&
        config.input_path.isEmpty()) {
        QMessageBox::critical(nullptr, QStringLiteral("Invalid arguments"),
                              QStringLiteral("--source nv12 requires --input"));
        return 2;
    }
    if (!parse_positive_even(parser.value("width"), config.width) ||
        !parse_positive_even(parser.value("height"), config.height)) {
        QMessageBox::critical(nullptr, QStringLiteral("Invalid arguments"),
                              QStringLiteral("Width/height must be positive even numbers"));
        return 2;
    }
    bool fps_ok = false;
    config.fps = parser.value("fps").toInt(&fps_ok);
    if (!fps_ok || config.fps < 1 || config.fps > 120) {
        QMessageBox::critical(nullptr, QStringLiteral("Invalid arguments"),
                              QStringLiteral("FPS must be in range 1..120"));
        return 2;
    }
    bool display_fps_ok = false;
    config.display_fps = parser.value("display-fps").toInt(&display_fps_ok);
    bool inference_fps_ok = false;
    config.inference_fps = parser.value("inference-fps").toInt(&inference_fps_ok);
    if (!display_fps_ok || config.display_fps < 1 || config.display_fps > 60 ||
        !inference_fps_ok || config.inference_fps < 1 ||
        config.inference_fps > 60) {
        QMessageBox::critical(nullptr, QStringLiteral("Invalid arguments"),
                              QStringLiteral("Display/inference FPS must be in range 1..60"));
        return 2;
    }
    if (!parse_positive_even(parser.value("work-width"), config.work_width)) {
        QMessageBox::critical(nullptr, QStringLiteral("Invalid arguments"),
                              QStringLiteral("Work width must be a positive even number"));
        return 2;
    }
    bool npu_workers_ok = false;
    config.npu_workers = parser.value("npu-workers").toInt(&npu_workers_ok);
    if (!npu_workers_ok || config.npu_workers < 1 || config.npu_workers > 3) {
        QMessageBox::critical(nullptr, QStringLiteral("Invalid arguments"),
                              QStringLiteral("NPU workers must be in range 1..3"));
        return 2;
    }

    std::vector<CameraEntry> cameras;
    if (parser.value("source") == QStringLiteral("all")) {
        try {
            cameras = load_camera_config(parser.value("config"), config);
        } catch (const std::exception& error) {
            QMessageBox::critical(nullptr, QStringLiteral("Camera configuration"),
                                  QString::fromUtf8(error.what()));
            return 2;
        }
    } else {
        cameras.push_back({QStringLiteral("Camera"), std::move(config)});
    }
    MainWindow window(std::move(cameras));
    window.show();
    if (parser.isSet("auto-start"))
        QTimer::singleShot(0, &window, &MainWindow::start_all);
    return app.exec();
}
