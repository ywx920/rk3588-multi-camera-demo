# RK3588 Multi-Camera Demo

面向ZX2603-2 RK3588板卡的多路摄像头采集、显示与RKNN实时目标检测工程。

当前支持：

- J7 OV13850；
- J8 OV13850；
- UC325 USB摄像头；
- V4L2/RKISP多路采集；
- RGA缩放及NV12到RGB转换；
- YOLOv5 RKNN实时检测；
- Qt 2×2固定监控界面；
- Web多路MJPEG监控界面；
- 每路及全局摄像头、推理开关；
- 后台启动、统一停止和稳定性监控；
- OV13850 IQ配置、板端检查脚本和完整调试记录。

## 当前板端环境

| 项目 | 当前配置 |
|---|---|
| SoC | Rockchip RK3588 |
| 板卡 | ZX2603-2 |
| 内核 | Linux 5.10.160 |
| 用户空间 | Ubuntu 20.04 ARM64 / GNOME |
| NPU | RKNPU 0.9.8，IOMMU模式 |
| RKNN模型 | YOLOv5s ReLU INT8，RK3588 |
| 摄像头 | J7/J8 OV13850 + UC325 |

## 目录

```text
2-demoRK/
├── doc/                    调试记录、当前基线和后续路线
├── zx-camera-minimal/      V4L2最小采集与IQ验证
├── zx-camera-qt-demo/      Qt多路预览与RKNN检测
├── zx-camera-web-demo/     Web多路预览与RKNN检测
├── j7-yolov5-rknn-pc/     PC模型准备及板端Runner
├── scripts/                公共检查脚本
└── stop_camera_demos.sh    板端统一停止入口
```

## 当前设备映射

不要依赖易变化的`/dev/videoN`编号。当前使用稳定别名：

```text
J7 -> /dev/video-camera0 -> rkisp0-vir2 mainpath
J8 -> /dev/video-camera1 -> rkisp1-vir1 mainpath
UC325 -> 通过sysfs名称和V4L index动态发现
```

J7/J8的RAW端为BG10；应用预览和推理使用RKISP mainpath的NV12输出。

## Qt Demo

板端构建：

```bash
cd /home/zxwy/zx-camera-qt-demo
bash scripts/build_board.sh
```

后台启动：

```bash
bash scripts/start_background.sh
```

Qt需要板端已经登录GNOME图形会话。Qt与Web会独占相同摄像头节点，不能同时运行。

主要特点：

- J7/J8位于顶部、UC325位于左下、右下预留第四路；
- 摄像头软暂停时继续DQBUF/QBUF，避免反复Start/Stop后等待帧；
- 集中RKNN调度器使用1至3个worker，每通道只保留最新帧；
- 支持逐路/全局推理控制、IoU跟踪、框平滑和空场景自适应降频；
- 支持V4L2 DMABUF到RGA路径。

详细说明见[Qt Demo README](zx-camera-qt-demo/README.md)。

## Web Demo

板端构建和启动：

```bash
cd /home/zxwy/zx-camera-web-demo
bash scripts/build_board.sh
bash scripts/start_background.sh
```

浏览器访问：

```text
http://192.168.10.65:8080/
```

主要特点：

- JSON动态生成J7、J8、UC325通道；
- 支持逐路和全局摄像头/推理控制；
- 没有浏览器订阅时跳过转换、推理和JPEG编码；
- 默认J7/J8约7.5 FPS、UC325约10 FPS发布；
- MPP RGB888 JPEG实验路径通过完整帧校验，失败时安全回退Qt JPEG。

详细说明见[Web Demo README](zx-camera-web-demo/README.md)。

## Minimal Demo

Minimal用于独立验证V4L2节点、格式、帧率、双OV13850并发和IQ配置。遇到Qt或Web
无画面时，应先用Minimal判断问题位于摄像头链路还是应用状态机。

```bash
cd /home/zxwy/zx-camera-minimal
bash scripts/run_all_cameras.sh
```

详细说明见[Minimal README](zx-camera-minimal/README.md)。

## 统一停止

```bash
/home/zxwy/stop_camera_demos.sh
```

## 模型

仓库包含：

```text
j7-yolov5-rknn-pc/models/source/yolov5s_relu_official.onnx
j7-yolov5-rknn-pc/models/generated/yolov5s_relu_rk3588_int8.rknn
```

RKNN模型必须与RK3588目标平台、板端`librknnrt.so`和RKNPU驱动兼容。

## 文档入口

- [完整调试过程与当前基线](doc/08-current-baseline-and-complete-debug-timeline-2026-08-06.md)
- [Qt与Web下一阶段路线](doc/09-qt-web-next-development-roadmap-2026-08-06.md)
- [全部文档索引](doc/README.md)
- [GitHub备份操作](GITHUB_BACKUP.md)

## 当前限制

- Qt和Web不能同时占用同一摄像头；
- Web当前可靠路径仍为软件JPEG，不建议直接扩展到8路；
- MPP硬件编码必须经过连续完整帧解码和浏览器验证，不能只检查JPEG头；
- RKNPU功耗模型数据仍不完整，但当前实际推理工作正常；
- 大型SDK、虚拟环境、下载缓存和RKISP Tuner工具不纳入Git仓库。

## 开发与提交

```bash
git status
git add -A
git diff --cached --stat
git commit -m "描述本次修改"
git push
```

本工程首先保证双OV13850链路、重复启停和完整码流稳定，再进行零拷贝、硬件编解码
及4至8路扩展。
