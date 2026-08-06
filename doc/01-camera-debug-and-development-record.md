# ZX2603-2 摄像头调试与开发记录

## 1. 文档范围

- 记录日期：2026-07-27 ～ 2026-07-30
- 板卡：ZX2603-2
- SoC：RK3588
- 板端系统：Ubuntu 20.04.6 LTS，aarch64
- 内核：Linux 5.10.160
- MIPI 摄像头：OV13850，ZC-OV13850R2A-V1 / Largan-50064B31
- USB 摄像头：Zhejiang Dahua UC325，USB ID `2e03:2200`

本记录只陈述已经观察或验证的结果。兼容 IQ 能让 RKAIQ 运行，不等于完成
模组画质标定。

## 2. 开发原则

整个过程采用最小闭环：

1. 先验证单个 V4L2 节点和原始帧；
2. 再增加 Qt 后台采集接口；
3. OV13850 画质阻塞时，不同时改动 ISP、Qt 和网络；
4. 新接入 UC325 后，先验证其原生 MJPEG；
5. Web 预览直接转发 UC325 MJPEG，不引入编码器。

## 3. 阶段一：V4L2 最小采集

PC 项目：

```text
/home/wx/2-demoRK/zx-camera-minimal
```

板端工具：

```text
/home/zxwy/zx-camera-tools
```

OV13850 已确认的输出：

```text
节点            /dev/video22
驱动            rkisp_v6
输出            rkisp_mainpath
缓冲类型        VIDEO_CAPTURE_MPLANE
格式            NV12，单平面
分辨率          2112×1568
bytesperline    2112
sizeimage       4,967,424 bytes
帧率            约 30 FPS
```

已完成：

- `QUERYCAP`、格式枚举、`G_FMT/S_FMT`；
- MMAP 缓冲申请、映射、全入队；
- `poll/DQBUF/QBUF` 连续采集；
- 原始帧保存、序号和 FPS 统计；
- SIGINT、STREAMOFF、munmap、close；
- 退出后立即重新打开设备。

早期序号统计把首次 `0 -> 1` 误报为 gap，之后已拆分为：

```text
Sequence gaps
Duplicate sequences
Out-of-order sequences
```

RKAIQ 启动后偶尔在流启动瞬间出现一次跳号，稳态帧序号连续。长时间验收仍应
独立统计启动期和稳态期。

## 4. OV13850 RKAIQ/IQ 排障

板端运行时识别：

```text
ISP HW ver: 30
AIQ v5.0x3.0
IQ Parser Calib v1.4.8
```

通过 `strace` 确认服务实际加载：

```text
/etc/iqfiles/ov13850_ZC-OV13850R2A-V1_Largan-50064B31.json
```

原始文件 SHA256：

```text
eba8f8cbb2b8c4473ccf61d7d899905581cf4d39c132315e8ed4ce98eea358f0
```

原文件是 `scene_isp21`，在 ISP30/AIQ v5.0x3.0 下出现：

```text
XCORE:E:invalid main scene len!
AWB:E:lost smartRun para in json
Segmentation fault
```

### 4.1 兼容 IQ

兼容测试经历：

- 直接使用不完整 ISP30 文件：服务崩溃；
- v2：服务和采集恢复，但 LSC 分区报错；
- v3：修正 2112×1568 LSC 分区，服务可持续运行。

v3 文件：

```text
/home/wx/2-demoRK/zx-camera-minimal/iq-test/ov13850_ZC-isp30-compat-v3.json
```

SHA256：

```text
17477115021e7f2c5c7632ab3af1df5c141952935547a45de6f7905d66b554c2
```

一次安装验证：

```text
180 frames
29.19 FPS
3 saved frames
RKAIQ active (running)
result=PASS_AND_INSTALLED
```

### 4.2 当前未解决问题

画面表现：

- 整体严重偏暗；
- 灯光区域偏黄；
- 暗部大面积压黑；
- 细节不可用。

这说明当前文件只解决“结构兼容与服务运行”，没有解决该模组的：

- AE 目标和曝光策略；
- AWB 光源模型；
- CCM；
- Gamma/DRC；
- LSC 实际标定；
- 黑电平、降噪和锐化。

因此不得把兼容 IQ 作为量产 IQ。

## 5. RKISP Tuner 连接记录

板端 `rkaiq_tool_server-V2.2.5-aarch64` 能够：

- 发现两个 OV13850 sensor；
- 映射 `/dev/video22`、`/dev/video31` 等 ISP 节点；
- 连接 `/tmp/UNIX.domain0`；
- 监听 TCP 5543；
- 接收到 Windows 主机 `192.168.10.33` 的连接。

网络也已验证：

```text
192.168.10.65:5543 reachable
```

但 Windows Tuner 表现为 Connect 灰置、Get AIQ Version 不可用并反复：

```text
Connected.
Disconnected.
```

结论：问题不在基础 IP/TCP 连通性，而在 Tuner、tool server、AIQ 协议或
UI 配置版本组合。2026 版 Tuner 中没有与板端 AIQ v5.0x3.0 明确匹配的
RK3588 配置，未继续做无边界试错。

## 6. 阶段二 Qt Demo

项目：

```text
/home/wx/2-demoRK/zx-camera-qt-demo
```

保留的输入：

- synthetic；
- NV12 文件；
- OV13850：`/dev/video22`、2112×1568、NV12；
- UC325：持久化 by-id、1280×720、MJPG。

实现特点：

- 采集在工作线程，UI 线程不执行 V4L2 阻塞操作；
- V4L2 使用 MMAP；
- UC325 MJPEG 用 Qt JPEG 插件解码；
- OV13850 NV12 使用独立 CPU 转换模块；
- 不依赖 OpenCV；
- 工作线程和设备资源可安全停止。

PC 编译通过。板端已有 Qt5 运行库和插件，但缺少 `cmake`、`qmake` 和 Qt5
开发文件，而且板端不能联网，因此板端 Qt 编译验收尚未完成。

## 7. UC325 USB 摄像头

节点：

```text
/dev/video40    Video Capture
/dev/video41    Metadata
```

推荐使用持久化路径：

```text
/dev/v4l/by-id/usb-Zhejiang_Dahua_Technology_Co._Ltd_Webcam_UC325_C0335D3A1E1B3F268-video-index0
```

主要能力：

- MJPG：320×240、640×480、1280×720、1920×1080，最高 30 FPS；
- YUYV：640×480 30 FPS、1280×720 10 FPS、1920×1080 5 FPS；
- H.264/HEVC：最高 1080p30。

1280×720 MJPG 最小测试：

```text
300 frames
30.29 FPS
sequence gaps=0
duplicates=0
out-of-order=0
3 valid JPEG files
```

样本：

```text
/home/wx/2-demoRK/zx-camera-minimal/usb-samples/uc325-1280x720-sample.jpg
```

## 8. UC325 Web 预览

项目：

```text
/home/wx/2-demoRK/zx-camera-web-demo
```

板端：

```text
/home/zxwy/zx-camera-web-demo
```

数据链：

```text
UC325 MJPEG -> V4L2 MMAP -> latest JPEG -> HTTP multipart MJPEG -> Browser
```

不依赖 Qt、CMake、OpenCV、GStreamer 或 Rockchip 专有库，直接用板端 `c++`
和 Makefile 编译。

2026-07-27 验证：

```text
1280×720
约 30～31 FPS
2 秒收到约 9.3 MB MJPEG
HTTP /status 正常
SIGINT 后进程退出
8080 端口释放
摄像头可重新打开
```

访问：

```text
http://192.168.10.65:8080/
```

管理：

```bash
cd /home/zxwy/zx-camera-web-demo
bash scripts/start_background.sh
bash scripts/stop_background.sh
curl http://127.0.0.1:8080/status
```

## 9. 当前基线

可作为后续回归基线：

1. OV13850 原始采集链路可用，但画质未通过；
2. RKAIQ 可用兼容 IQ 启动，但 IQ 不是正式标定；
3. UC325 的采集和 Web 预览闭环已通过；
4. Qt 源码已具备双接口，板端构建依赖尚未离线补齐；
5. 后续修改 BSP 或切换 SDK 时，必须先复测 V4L2 最小采集，再验证 3A，
   最后才恢复 Qt/Web/AI。
