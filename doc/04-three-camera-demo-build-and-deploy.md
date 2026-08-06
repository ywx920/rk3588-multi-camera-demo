# ZX2603-2 三路摄像头 Demo 构建与部署

## 设备模型

- J7 OV13850：`/dev/video-camera0`，NV12 2112×1568。
- J8 OV13850：`/dev/video-camera1`，NV12 2112×1568。
- UC325：按 `/sys/class/video4linux/video*/name` 自动发现 Capture 节点，MJPG 1280×720。

不要把 OV13850 RAW 节点 `/dev/video0`、`/dev/video11` 交给应用，它们由
`rkaiq_3A_server` 占用。应用只打开两个 `rkisp_mainpath` 稳定别名。

## Ubuntu/Debian 板端开发环境

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake pkg-config \
  qtbase5-dev qtbase5-dev-tools qt5-qmake \
  libjpeg-dev v4l-utils
```

运行 Qt/X11 预览还需要系统已有的 Qt XCB 插件；当前镜像已经包含
`libqxcb.so` 和 `libqjpeg.so`。板卡不能联网时，应在 SDK 制作 rootfs 阶段把以上
包加入镜像，不要只复制头文件。

## 构建

```bash
cd /home/zxwy/zx-camera-minimal
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4

cd /home/zxwy/zx-camera-web-demo
make clean all

cd /home/zxwy/zx-camera-qt-demo
cmake -S . -B build-board -DCMAKE_BUILD_TYPE=Release
cmake --build build-board -j4
```

## 运行

```bash
cd /home/zxwy/zx-camera-minimal
bash scripts/run_all_cameras.sh

cd /home/zxwy/zx-camera-web-demo
bash scripts/run_uc325_web.sh
# 浏览器：http://192.168.10.65:8080/

cd /home/zxwy/zx-camera-qt-demo
bash scripts/run_board_all.sh
```

Qt 程序必须从板端桌面终端运行，或在 SSH 中正确传递桌面会话的 `DISPLAY`、
`XAUTHORITY` 和 `XDG_RUNTIME_DIR`。

Web 版本目前对两路 2112×1568 NV12 使用 CPU 转 RGB/JPEG，默认每三帧编码一帧，
约 10 FPS Web 输出；采集本身仍保持 30 FPS。后续可用 RGA 和 MPP/JPEG 硬件编码
降低 CPU 占用。
