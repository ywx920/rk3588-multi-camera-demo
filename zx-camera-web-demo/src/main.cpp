#include "jpeg_encoder.h"
#include "hardware_jpeg_encoder.h"
#include "v4l2_mjpeg_camera.h"
#include "nv12_converter.h"
#include "rknn_yolov5_engine.h"
#include "detection_types.h"

#include <QBuffer>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <regex>
#include <vector>

namespace {
std::atomic_bool running{true};
std::mutex rknn_initialization_mutex;
void signal_handler(int) { running.store(false); }

struct CameraConfig {
    std::string id;
    std::string title;
    std::string device;
    std::string format;
    int width{};
    int height{};
    int encode_every{1};
    bool detection{true};
    int inference_fps{5};
    int work_width{960};
    int jpeg_quality{72};
    std::string model_path;
};

struct SharedFrame {
    explicit SharedFrame(CameraConfig value)
        : config(std::move(value)), detection_enabled(config.detection) {}
    CameraConfig config;
    std::mutex mutex;
    std::shared_ptr<const std::vector<std::uint8_t>> jpeg;
    std::uint64_t frames{};
    double fps{};
    std::string error;
    std::atomic_bool camera_enabled{true};
    std::atomic_bool detection_enabled{true};
    std::atomic_int viewers{0};
    double inference_ms{};
    double inference_fps{};
    std::size_t objects{};
    std::string encoder{"pending"};
};

std::string json_string(const std::string& object, const std::string& key,
                        const std::string& fallback = {})
{
    const std::regex expression("\\\"" + key +
        "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch match;
    return std::regex_search(object, match, expression) ? match[1].str()
                                                         : fallback;
}

int json_int(const std::string& object, const std::string& key, int fallback)
{
    const std::regex expression("\\\"" + key + "\\\"\\s*:\\s*([0-9]+)");
    std::smatch match;
    return std::regex_search(object, match, expression)
        ? std::stoi(match[1].str()) : fallback;
}

bool json_bool(const std::string& object, const std::string& key, bool fallback)
{
    const std::regex expression("\\\"" + key + "\\\"\\s*:\\s*(true|false)");
    std::smatch match;
    return std::regex_search(object, match, expression)
        ? match[1].str() == "true" : fallback;
}

std::vector<CameraConfig> load_camera_config(const std::string& path,
                                             int ov_encode_every,
                                             const std::string& model_path)
{
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open camera config: " + path);
    const std::string json((std::istreambuf_iterator<char>(input)), {});
    const std::regex object_expression("\\{[^\\{\\}]*\\}");
    std::vector<CameraConfig> result;
    for (std::sregex_iterator it(json.begin(), json.end(), object_expression), end;
         it != end; ++it) {
        const std::string object = it->str();
        const std::string id = json_string(object, "id");
        if (id.empty() || !json_bool(object, "enabled", true)) continue;
        const std::string type = json_string(object, "type");
        const std::string format = json_string(
            object, "format", type == "uc325" ? "MJPG" : "NV12");
        result.push_back({id, json_string(object, "title", id),
            json_string(object, "device",
                type == "uc325" ? "/dev/video20" : std::string{}),
            format, json_int(object, "width", format == "MJPG" ? 1280 : 2112),
            json_int(object, "height", format == "MJPG" ? 720 : 1568),
            json_int(object, "encode_every",
                     format == "MJPG" ? 2 : ov_encode_every),
            json_bool(object, "inference", true),
            json_int(object, "inference_fps", 5),
            json_int(object, "work_width", 960),
            std::clamp(json_int(object, "jpeg_quality", 72), 35, 95),
            model_path});
    }
    if (result.empty()) throw std::runtime_error("camera config contains no enabled camera");
    return result;
}

std::vector<std::uint8_t> image_to_jpeg(const QImage& image, int quality)
{
    QByteArray encoded;
    QBuffer buffer(&encoded);
    if (!buffer.open(QIODevice::WriteOnly) ||
        !image.save(&buffer, "JPG", quality))
        throw std::runtime_error("Qt JPEG encoding failed");
    return {reinterpret_cast<const std::uint8_t*>(encoded.constData()),
            reinterpret_cast<const std::uint8_t*>(encoded.constData()) +
                encoded.size()};
}

void draw_detections(QImage& image, const SharedInferenceResult& result)
{
    if (!result) return;
    QPainter painter(&image);
    painter.setPen(QPen(QColor(45, 245, 155),
                        std::max(2, image.width() / 420)));
    QFont font = painter.font();
    font.setPixelSize(std::max(16, image.width() / 55));
    font.setBold(true);
    painter.setFont(font);
    for (const auto& item : result->detections) {
        painter.drawRect(item.box);
        const QString label = QStringLiteral("%1 #%2 %3")
            .arg(item.label).arg(item.track_id)
            .arg(item.confidence, 0, 'f', 2);
        QRectF box = painter.boundingRect(QRectF(), Qt::AlignLeft, label)
                         .adjusted(-5, -3, 5, 3);
        box.moveBottomLeft(QPointF(item.box.left(),
                                  std::max(box.height(), item.box.top())));
        painter.fillRect(box, QColor(3, 12, 24, 205));
        painter.drawText(box.adjusted(5, 3, -5, -3), label);
    }
}

SharedInferenceResult track_detections(SharedInferenceResult fresh,
                                       const SharedInferenceResult& previous,
                                       int& next_track_id)
{
    if (!fresh) return fresh;
    auto tracked = std::make_shared<InferenceResult>(*fresh);
    for (auto& current : tracked->detections) {
        const Detection* best = nullptr;
        qreal best_iou = 0.0;
        if (previous) for (const auto& old : previous->detections) {
            if (old.label != current.label) continue;
            const QRectF intersection = old.box.intersected(current.box);
            const qreal intersection_area = intersection.width() * intersection.height();
            const qreal union_area = old.box.width() * old.box.height() +
                current.box.width() * current.box.height() - intersection_area;
            const qreal iou = union_area > 0.0 ? intersection_area / union_area : 0.0;
            if (iou > best_iou) { best_iou = iou; best = &old; }
        }
        if (best && best_iou >= 0.25) {
            current.track_id = best->track_id;
            constexpr qreal old_weight = 0.35;
            current.box = QRectF(
                best->box.x() * old_weight + current.box.x() * (1.0 - old_weight),
                best->box.y() * old_weight + current.box.y() * (1.0 - old_weight),
                best->box.width() * old_weight + current.box.width() * (1.0 - old_weight),
                best->box.height() * old_weight + current.box.height() * (1.0 - old_weight));
        } else current.track_id = next_track_id++;
    }
    return tracked;
}

std::string find_uc325(const std::string& fallback)
{
    const std::filesystem::path root("/sys/class/video4linux");
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
        std::ifstream name(entry.path() / "name");
        std::string text;
        std::getline(name, text);
        std::ifstream index_file(entry.path() / "index");
        int index = -1;
        index_file >> index;
        if (text.find("UC325") != std::string::npos && index == 0)
            return "/dev/" + entry.path().filename().string();
    }
    return fallback;
}

bool send_all(int fd, const void* data, std::size_t size)
{
    const auto* bytes = static_cast<const char*>(data);
    while (size > 0U && running.load()) {
        const ssize_t sent = send(fd, bytes, size, MSG_NOSIGNAL);
        if (sent < 0) { if (errno == EINTR) continue; return false; }
        bytes += sent;
        size -= static_cast<std::size_t>(sent);
    }
    return size == 0U;
}

std::string page(const std::vector<std::unique_ptr<SharedFrame>>& cameras)
{
    std::ostringstream cards;
    for (const auto& camera : cameras) {
        cards << "<article class=\"card\"><div class=\"cardHead\"><div>"
              << "<span class=\"live\"></span><h2>" << camera->config.title
              << "</h2></div><span class=\"tag\">" << camera->config.format
              << "</span></div><div class=\"viewport\"><img src=\"/"
              << camera->config.id << ".mjpg\" loading=\"lazy\"></div>"
              << "<div class=\"bottom\"><div id=\"" << camera->config.id
              << "\" class=\"status\">正在连接视频流…</div>"
              << "<button class=\"camera\" data-id=\"" << camera->config.id
              << "\">摄像头开启</button><button class=\"detect\" data-id=\""
              << camera->config.id << "\">检测开启</button></div></article>";
    }
    return R"(<!doctype html><html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ZX Vision · AI Camera Console</title><style>
:root{color-scheme:dark;--bg:#080d19;--panel:#111a2d;--line:#253452;--muted:#91a0ba;--cyan:#61dafb;--green:#38e39f}
*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at 15% 0,#132445 0,transparent 32%),var(--bg);color:#edf3ff;font-family:Inter,"Noto Sans SC",system-ui,sans-serif;min-height:100vh}
header{padding:28px clamp(18px,4vw,56px) 14px;display:flex;align-items:end;justify-content:space-between;gap:20px}h1{font-size:clamp(24px,3vw,38px);margin:0;letter-spacing:-.03em}.sub{color:var(--muted);margin-top:7px}.headerRight{display:flex;align-items:center;gap:9px;flex-wrap:wrap;justify-content:flex-end}.health{background:#102b29;border:1px solid #1f5c50;color:#73f2c2;padding:8px 13px;border-radius:999px;font-size:13px}.allDetect,.allCamera{border:1px solid #76509a;background:#40265c;color:#e7c5ff;border-radius:9px;padding:8px 12px;cursor:pointer;font-weight:700}.allCamera{border-color:#27648a;background:#153449;color:#78cbed}.allDetect.off,.allCamera.off{border-color:#8c4550;background:#592832;color:#ffc0c8}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,380px),1fr));gap:18px;padding:18px clamp(18px,4vw,56px) 42px}.card{background:linear-gradient(145deg,rgba(24,35,59,.96),rgba(13,21,38,.96));border:1px solid var(--line);border-radius:18px;padding:14px;box-shadow:0 18px 50px #0007;overflow:hidden}.cardHead{display:flex;align-items:center;justify-content:space-between;padding:3px 3px 12px}.cardHead>div{display:flex;align-items:center;gap:9px}h2{font-size:17px;margin:0}.live{width:9px;height:9px;border-radius:50%;background:var(--green);box-shadow:0 0 14px var(--green)}.card.cameraOff .live{background:#647089;box-shadow:none}.tag{font:12px ui-monospace,monospace;color:var(--cyan);background:#142941;border:1px solid #244565;padding:4px 8px;border-radius:7px}.viewport{position:relative;background:#02050a;border:1px solid #263755;border-radius:12px;overflow:hidden;aspect-ratio:4/3;display:grid;place-items:center}.card.cameraOff .viewport:after{content:'CAMERA OFF';position:absolute;color:#8e9ab0;font:600 15px ui-monospace,monospace;letter-spacing:.12em}.card.cameraOff img{opacity:.08}img{width:100%;height:100%;object-fit:contain}.bottom{display:flex;align-items:center;gap:9px;padding-top:9px;flex-wrap:wrap}.status{color:var(--muted);font:12px ui-monospace,monospace;flex:1;min-width:180px}.error{color:#ff8291}.detect,.camera{border:1px solid #26775f;background:#123d34;color:#67efbd;border-radius:8px;padding:6px 10px;cursor:pointer}.camera{border-color:#27648a;background:#153449;color:#78cbed}.detect.off,.camera.off,.detect:disabled{border-color:#465269;background:#222b3c;color:#91a0ba}.detect:disabled{cursor:not-allowed}footer{color:#66758f;text-align:center;padding:0 20px 24px;font-size:12px}
@media(max-width:600px){header{align-items:start;flex-direction:column}.grid{padding-top:8px}.card{border-radius:14px}}
</style></head><body><header><div><h1>ZX Vision Console</h1><div class="sub">RK3588 多路实时视觉监控</div></div><div class="headerRight"><button id="cameraAllOn" class="allCamera">开启全部摄像头</button><button id="cameraAllOff" class="allCamera off">关闭全部摄像头</button><button id="allOn" class="allDetect">开启全部推理</button><button id="allOff" class="allDetect off">一键关闭推理</button><div id="health" class="health">● 服务在线</div></div></header><main class="grid">)" + cards.str() + R"(</main><footer>ZX2603-2 · Dynamic V4L2 Streaming</footer><script>
async function update(){try{const all=await fetch('/status',{cache:'no-store'}).then(r=>r.json());for(const x of all){const e=document.getElementById(x.id);if(!e)continue;const card=e.closest('.card');card.classList.toggle('cameraOff',!x.camera);e.classList.toggle('error',!!x.error);e.textContent=x.error||(!x.camera?'摄像头已暂停':`${x.width}×${x.height} · ${x.fps.toFixed(1)} FPS · NPU ${x.inference_ms.toFixed(1)} ms · ${x.objects} obj · ${x.encoder}`);const d=card.querySelector('.detect');d.classList.toggle('off',!x.detection);d.textContent=x.detection?'检测开启':'检测关闭';d.disabled=!x.camera;const c=card.querySelector('.camera');c.classList.toggle('off',!x.camera);c.textContent=x.camera?'摄像头开启':'摄像头关闭';}document.getElementById('health').textContent=`● ${all.filter(x=>x.camera).length}/${all.length} 路在线 · ${all.filter(x=>x.detection).length} 路推理`;}catch(e){document.getElementById('health').textContent='● 连接中断';}}document.querySelectorAll('.detect').forEach(b=>b.onclick=async()=>{const enable=b.classList.contains('off')?1:0;await fetch(`/toggle?id=${b.dataset.id}&enabled=${enable}`,{method:'POST'});update();});document.querySelectorAll('.camera').forEach(b=>b.onclick=async()=>{const enable=b.classList.contains('off')?1:0;await fetch(`/camera-toggle?id=${b.dataset.id}&enabled=${enable}`,{method:'POST'});update();});document.getElementById('cameraAllOn').onclick=async()=>{await fetch('/camera-all?enabled=1',{method:'POST'});update();};document.getElementById('cameraAllOff').onclick=async()=>{await fetch('/camera-all?enabled=0',{method:'POST'});update();};document.getElementById('allOn').onclick=async()=>{await fetch('/inference-all?enabled=1',{method:'POST'});update();};document.getElementById('allOff').onclick=async()=>{await fetch('/inference-all?enabled=0',{method:'POST'});update();};setInterval(update,1000);update();
</script></body></html>)";
}

SharedFrame* find_camera(const std::string& path,
                         const std::vector<std::unique_ptr<SharedFrame>>& cameras)
{
    for (const auto& camera : cameras)
        if (path == "/" + camera->config.id + ".mjpg") return camera.get();
    return nullptr;
}

SharedFrame* find_camera_id(
    const std::string& id,
    const std::vector<std::unique_ptr<SharedFrame>>& cameras)
{
    for (const auto& camera : cameras)
        if (camera->config.id == id) return camera.get();
    return nullptr;
}

void stream_client(int client, SharedFrame& shared)
{
    const std::string header = "HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=frame\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    if (!send_all(client, header.data(), header.size())) return;
    struct ViewerGuard {
        explicit ViewerGuard(std::atomic_int& value) : count(value) {
            count.fetch_add(1);
        }
        ~ViewerGuard() { count.fetch_sub(1); }
        std::atomic_int& count;
    } viewer_guard(shared.viewers);
    std::uint64_t last = 0;
    while (running.load()) {
        std::shared_ptr<const std::vector<std::uint8_t>> jpeg;
        std::uint64_t current{};
        { std::lock_guard<std::mutex> lock(shared.mutex); jpeg = shared.jpeg; current = shared.frames; }
        if (jpeg && current != last) {
            const std::string part = "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: " +
                std::to_string(jpeg->size()) + "\r\n\r\n";
            if (!send_all(client, part.data(), part.size()) ||
                !send_all(client, jpeg->data(), jpeg->size()) ||
                !send_all(client, "\r\n", 2U)) break;
            last = current;
        } else std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void client_session(int client, std::vector<std::unique_ptr<SharedFrame>>& cameras)
{
    timeval timeout{3, 0};
    (void)setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    char request[2048]{};
    const ssize_t count = recv(client, request, sizeof(request) - 1U, 0);
    if (count <= 0) { close(client); return; }
    const std::string text(request, static_cast<std::size_t>(count));
    const auto first = text.find(' ');
    const auto second = first == std::string::npos ? first : text.find(' ', first + 1U);
    const std::string path = first == std::string::npos || second == std::string::npos
        ? "/" : text.substr(first + 1U, second - first - 1U);
    if (path == "/") {
        const std::string body = page(cameras);
        const std::string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
        (void)send_all(client, header.data(), header.size());
        (void)send_all(client, body.data(), body.size());
    } else if (path == "/status") {
        std::ostringstream body;
        body << '[';
        for (std::size_t index = 0; index < cameras.size(); ++index) {
            auto& item = *cameras[index];
            std::lock_guard<std::mutex> lock(item.mutex);
            const bool camera_on = item.camera_enabled.load();
            if (index) body << ',';
            body << "{\"id\":\"" << item.config.id << "\",\"width\":" << item.config.width
                 << ",\"height\":" << item.config.height << ",\"fps\":" << item.fps
                 << ",\"frames\":" << item.frames
                 << ",\"viewers\":" << item.viewers.load()
                 << ",\"camera\":"
                 << (camera_on ? "true" : "false")
                 << ",\"detection\":"
                 << (item.detection_enabled.load() ? "true" : "false")
                 << ",\"inference_ms\":" << (camera_on ? item.inference_ms : 0.0)
                 << ",\"inference_fps\":" << (camera_on ? item.inference_fps : 0.0)
                 << ",\"objects\":" << (camera_on ? item.objects : 0U)
                 << ",\"encoder\":\"" << item.encoder << "\""
                 << ",\"error\":\"" << item.error << "\"}";
        }
        body << ']';
        const std::string data = body.str();
        const std::string header = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nContent-Length: " + std::to_string(data.size()) + "\r\nConnection: close\r\n\r\n";
        (void)send_all(client, header.data(), header.size());
        (void)send_all(client, data.data(), data.size());
    } else if (path.rfind("/inference-all?", 0) == 0 ||
               path.rfind("/camera-all?", 0) == 0) {
        const bool camera_all = path.rfind("/camera-all?", 0) == 0;
        const auto enabled_start = path.find("enabled=");
        if (enabled_start == std::string::npos ||
            enabled_start + 8U >= path.size()) {
            const char response[] = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            (void)send_all(client, response, sizeof(response) - 1U);
        } else {
            const bool enabled = path[enabled_start + 8U] == '1';
            for (auto& camera : cameras) {
                if (camera_all) camera->camera_enabled.store(enabled);
                else camera->detection_enabled.store(enabled);
                if (!enabled || camera_all) {
                    std::lock_guard<std::mutex> lock(camera->mutex);
                    camera->inference_ms = 0.0;
                    camera->inference_fps = 0.0;
                    camera->objects = 0;
                }
            }
            const char response[] = "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n";
            (void)send_all(client, response, sizeof(response) - 1U);
        }
    } else if (path.rfind("/toggle?", 0) == 0 ||
               path.rfind("/camera-toggle?", 0) == 0) {
        const bool camera_toggle = path.rfind("/camera-toggle?", 0) == 0;
        const auto id_start = path.find("id=");
        const auto enabled_start = path.find("enabled=");
        SharedFrame* camera = nullptr;
        bool valid = id_start != std::string::npos &&
                     enabled_start != std::string::npos &&
                     enabled_start + 8U < path.size();
        if (valid) {
            const auto id_end = path.find('&', id_start);
            const std::string id = path.substr(
                id_start + 3U,
                (id_end == std::string::npos ? path.size() : id_end) -
                    id_start - 3U);
            camera = find_camera_id(id, cameras);
        }
        if (camera) {
            const char value = path[enabled_start + 8U];
            if (camera_toggle)
                camera->camera_enabled.store(value == '1');
            else
                camera->detection_enabled.store(value == '1');
            if (value != '1' || camera_toggle) {
                std::lock_guard<std::mutex> lock(camera->mutex);
                camera->inference_ms = 0.0;
                camera->inference_fps = 0.0;
                camera->objects = 0;
            }
            const char response[] = "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n";
            (void)send_all(client, response, sizeof(response) - 1U);
        } else {
            const char response[] = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            (void)send_all(client, response, sizeof(response) - 1U);
        }
    } else if (auto* camera = find_camera(path, cameras)) stream_client(client, *camera);
    else {
        const char response[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        (void)send_all(client, response, sizeof(response) - 1U);
    }
    close(client);
}

void capture_camera(SharedFrame& shared)
{
    try {
        V4L2Camera camera;
        camera.open(shared.config.device, shared.config.width, shared.config.height, shared.config.format);
        shared.config.width = camera.width();
        shared.config.height = camera.height();
        auto fps_window_started = std::chrono::steady_clock::now();
        std::uint64_t published_in_window = 0;
        bool was_publishing = false;
        auto idle_since = std::chrono::steady_clock::now();
        auto last_inference = std::chrono::steady_clock::time_point{};
        std::unique_ptr<RknnYolov5Engine> engine;
        SharedInferenceResult latest_result;
        int next_track_id = 1;
        int empty_detection_streak = 0;
        HardwareJpegEncoder hardware_jpeg;
        bool hardware_jpeg_failed = false;
        V4L2Camera::Frame frame;
        std::uint64_t captured{};
        while (running.load()) {
            const bool camera_enabled = shared.camera_enabled.load();
            const bool has_demand = camera_enabled && shared.viewers.load() > 0;
            const bool encode = has_demand &&
                captured % static_cast<unsigned int>(
                    shared.config.encode_every) == 0U;
            if (!camera.capture(frame, 500, encode)) continue;
            ++captured;
            if (!has_demand) {
                if (was_publishing) {
                    std::lock_guard<std::mutex> lock(shared.mutex);
                    shared.fps = 0.0;
                    shared.inference_ms = 0.0;
                    shared.inference_fps = 0.0;
                    shared.objects = 0;
                    latest_result.reset();
                    empty_detection_streak = 0;
                    next_track_id = 1;
                    idle_since = std::chrono::steady_clock::now();
                }
                if (engine && std::chrono::steady_clock::now() - idle_since >=
                                  std::chrono::seconds(30))
                    engine.reset();
                was_publishing = false;
                published_in_window = 0;
                fps_window_started = std::chrono::steady_clock::now();
            } else if (!was_publishing) {
                was_publishing = true;
                published_in_window = 0;
                fps_window_started = std::chrono::steady_clock::now();
            }
            if (!encode) continue;
            const auto now = std::chrono::steady_clock::now();
            const bool detection = shared.detection_enabled.load();
            std::vector<std::uint8_t> encoded;
            if (shared.config.format == "MJPG" && !detection) {
                encoded = frame.data;
                latest_result.reset();
                std::lock_guard<std::mutex> lock(shared.mutex);
                shared.encoder = "MJPEG passthrough";
            } else {
                QImage image;
                if (shared.config.format == "MJPG") {
                    image = QImage::fromData(
                        reinterpret_cast<const uchar*>(frame.data.data()),
                        static_cast<int>(frame.data.size()), "JPG")
                                .convertToFormat(QImage::Format_RGB888);
                    if (image.isNull())
                        throw std::runtime_error("cannot decode MJPG frame");
                    if (image.width() > shared.config.work_width) {
                        int output_height = image.height() *
                            shared.config.work_width / image.width();
                        output_height = std::max(2, output_height & ~1);
                        image = image.scaled(shared.config.work_width,
                                             output_height,
                                             Qt::IgnoreAspectRatio,
                                             Qt::FastTransformation);
                    }
                } else {
                    int output_height = shared.config.height *
                        shared.config.work_width / shared.config.width;
                    output_height = std::max(2, output_height & ~1);
                    image = Nv12Converter::to_rgb_scaled(
                        frame.data, shared.config.width, shared.config.height,
                        shared.config.work_width, output_height);
                }
                if (detection) {
                    if (!engine)
                    {
                        std::lock_guard<std::mutex> initialization_lock(
                            rknn_initialization_mutex);
                        engine = std::make_unique<RknnYolov5Engine>(
                            QString::fromStdString(shared.config.model_path));
                    }
                    int inference_multiplier = 1;
                    if (empty_detection_streak >= 30) inference_multiplier = 4;
                    else if (empty_detection_streak >= 10) inference_multiplier = 3;
                    else if (empty_detection_streak >= 3) inference_multiplier = 2;
                    const auto interval = std::chrono::milliseconds(std::min(
                        1500, inference_multiplier *
                            1000 / std::max(1, shared.config.inference_fps)));
                    if (!latest_result || now - last_inference >= interval) {
                        auto fresh = std::make_shared<const InferenceResult>(
                            engine->infer(image));
                        latest_result = track_detections(
                            std::move(fresh), latest_result, next_track_id);
                        empty_detection_streak = latest_result->detections.empty()
                            ? empty_detection_streak + 1 : 0;
                        last_inference = now;
                    }
                    draw_detections(image, latest_result);
                } else {
                    latest_result.reset();
                    empty_detection_streak = 0;
                    next_track_id = 1;
                }
                if (!hardware_jpeg_failed) {
                    try {
                        encoded = hardware_jpeg.encode(
                            image, shared.config.jpeg_quality);
                        std::lock_guard<std::mutex> lock(shared.mutex);
                        shared.encoder = "MPP MJPEG";
                    } catch (const std::exception& error) {
                        hardware_jpeg_failed = true;
                        std::cerr << shared.config.id
                                  << " MPP JPEG fallback: " << error.what()
                                  << '\n';
                    }
                }
                if (encoded.empty()) {
                    encoded = image_to_jpeg(
                        image, shared.config.jpeg_quality);
                    std::lock_guard<std::mutex> lock(shared.mutex);
                    shared.encoder = "Qt JPEG fallback";
                }
            }
            ++published_in_window;
            const double fps_window_seconds =
                std::chrono::duration<double>(now - fps_window_started).count();
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.jpeg = std::make_shared<const std::vector<std::uint8_t>>(std::move(encoded));
            ++shared.frames;
            if (fps_window_seconds >= 1.0) {
                shared.fps = published_in_window / fps_window_seconds;
                published_in_window = 0;
                fps_window_started = now;
            }
            shared.inference_ms = latest_result
                ? latest_result->inference_ms : 0.0;
            shared.inference_fps = latest_result
                ? latest_result->inference_fps : 0.0;
            shared.objects = latest_result
                ? latest_result->detections.size() : 0U;
        }
    } catch (const std::exception& error) {
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.error = error.what();
        std::cerr << shared.config.id << " error: " << error.what() << '\n';
    }
}

int positive(const char* value)
{
    const int parsed = std::stoi(value);
    if (parsed <= 0 || parsed > 65535) throw std::invalid_argument("invalid number");
    return parsed;
}
}

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QGuiApplication application(argc, argv);
    std::string j7 = "/dev/video-camera0", j8 = "/dev/video-camera1", uc325 = "/dev/video20";
    std::string config_path = "/home/zxwy/zx-camera-web-demo/config/cameras.json";
    std::string model_path =
        "/home/zxwy/j7-yolov5/models/yolov5s_relu_rk3588_int8.rknn";
    int port = 8080, ov_encode_every = 3;
    for (int i = 1; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--help") {
            std::cout << "Usage: " << argv[0] << " [--config FILE] [--model FILE] [--j7 PATH] [--j8 PATH] [--uc325 PATH] [--port N] [--ov-encode-every N]\n";
            return 0;
        }
        if (i + 1 >= argc) throw std::invalid_argument("missing value for " + option);
        if (option == "--j7") j7 = argv[++i];
        else if (option == "--j8") j8 = argv[++i];
        else if (option == "--uc325") uc325 = argv[++i];
        else if (option == "--port") port = positive(argv[++i]);
        else if (option == "--ov-encode-every") ov_encode_every = positive(argv[++i]);
        else if (option == "--config") config_path = argv[++i];
        else if (option == "--model") model_path = argv[++i];
        else throw std::invalid_argument("unknown option: " + option);
    }
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    auto configured = load_camera_config(config_path, ov_encode_every,
                                          model_path);
    for (auto& camera : configured) {
        if (camera.id == "j7") camera.device = j7;
        else if (camera.id == "j8") camera.device = j8;
        else if (camera.id == "uc325") camera.device = find_uc325(uc325);
    }
    std::vector<std::unique_ptr<SharedFrame>> cameras;
    for (auto& camera : configured)
        cameras.emplace_back(std::make_unique<SharedFrame>(std::move(camera)));
    std::vector<std::thread> capture_threads;
    for (auto& camera : cameras) capture_threads.emplace_back(capture_camera, std::ref(*camera));
    int server = -1;
    try {
        server = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (server < 0) throw std::runtime_error("socket failed");
        int yes = 1;
        (void)setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        address.sin_port = htons(static_cast<std::uint16_t>(port));
        if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0)
            throw std::system_error(errno, std::generic_category(), "bind");
        if (listen(server, 16) < 0) throw std::runtime_error("listen failed");
        std::cout << cameras.size() << "-camera preview: http://0.0.0.0:"
                  << port << "/\n";
        std::vector<std::thread> clients;
        while (running.load()) {
            fd_set set; FD_ZERO(&set); FD_SET(server, &set);
            timeval timeout{0, 500000};
            const int ready = select(server + 1, &set, nullptr, nullptr, &timeout);
            if (ready < 0) { if (errno == EINTR) continue; throw std::runtime_error("select failed"); }
            if (ready > 0) {
                const int client = accept4(server, nullptr, nullptr, SOCK_CLOEXEC);
                if (client >= 0) clients.emplace_back(client_session, client, std::ref(cameras));
            }
        }
        for (auto& client : clients) if (client.joinable()) client.join();
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        running.store(false);
    }
    if (server >= 0) close(server);
    for (auto& thread : capture_threads) if (thread.joinable()) thread.join();
    return 0;
}
