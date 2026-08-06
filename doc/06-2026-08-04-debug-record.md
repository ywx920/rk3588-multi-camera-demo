# 2026-08-04 ZX2603-2 调试记录

## 1. 今日目标

- 同时启用 J7、J8 两路 OV13850；
- 确认两路摄像头的媒体链路和稳定设备节点；
- 将 UC325、J7、J8 扩展到 minimal、Web、Qt 三套 Demo；
- 解决 Qt 单路及反复 Start/Stop 后进入 `Waiting for frame` 的问题；
- 初步确认 RK3588 NPU 环境，为实时目标检测做准备。

## 2. 双 OV13850 媒体链路确认

板端最终识别到两套独立链路：

| 接口 | Sensor | CIF / DPHY | ISP mainpath | 稳定别名 |
|---|---|---|---|---|
| J7 | `m00_b_ov13850 3-0010` | `rkcif-mipi-lvds2` / DPHY1 | `rkisp0-vir2`，`video22` | `/dev/video-camera0` |
| J8 | `m01_b_ov13850 2-0010` | `rkcif-mipi-lvds4` / DPHY4 | `rkisp1-vir1`，`video31` | `/dev/video-camera1` |

`/dev/media0`、`/dev/media1` 为两套 CIF，`/dev/media2`、`/dev/media3` 为两套 ISP。
所有 sensor → DPHY → CIF → ISP mainpath 链接均为 `ENABLED`。

应用应使用 `/dev/video-camera0` 和 `/dev/video-camera1` 的 NV12 mainpath，不能把
RAW 节点 `/dev/video0`、`/dev/video11` 当成普通应用采集节点。

## 3. RAW 节点 busy 的原因

检查发现：

```text
/dev/video0       -> rkaiq_3A_server
/dev/video11      -> rkaiq_3A_server
/dev/v4l-subdev2  -> rkaiq_3A_server
/dev/v4l-subdev7  -> rkaiq_3A_server
```

因此在 `/dev/video0` 上执行 `VIDIOC_S_FMT` 出现：

```text
rkcif_s_fmt_vid_cap_mplane queue busy
VIDIOC_S_FMT: Device or resource busy
```

这是 RKAIQ 为两路 sensor 执行 AE/AWB/ISP 参数控制时占用 RAW/子设备节点造成的，
不是两路摄像头不能同时工作。保持 `rkaiq_3A_server` 运行，并让应用只开 mainpath。

日志里的 `input mbus_code 0x3007, can't transform to NV12` 来自查询 RAW CIF 节点支持格式，
也不代表 ISP mainpath 的 NV12 失败。

## 4. J7 timeout 与恢复验证

J7 最初在 mainpath 上出现 `select timeout`。重启后使用 sensor test pattern 验证：

```bash
v4l2-ctl -d /dev/v4l-subdev2 --set-ctrl=test_pattern=1
v4l2-ctl -d /dev/video-camera0 \
  --set-fmt-video=width=2112,height=1568,pixelformat=NV12 \
  --stream-mmap=4 --stream-count=90 --stream-poll
v4l2-ctl -d /dev/v4l-subdev2 --set-ctrl=test_pattern=0
```

测试图和恢复正常图像后均达到约 `30.05 FPS`。这说明 J7 的 DPHY、CIF、ISP 和 DMA
链路能正常出帧；一次 timeout 更接近流状态残留或初始化时序问题。

## 5. 两路并发采集结果

并行启动：

```bash
v4l2-ctl -d /dev/video-camera0 \
  --set-fmt-video=width=2112,height=1568,pixelformat=NV12 \
  --stream-mmap=4 --stream-count=300 --stream-poll >/tmp/j7.log 2>&1 &

v4l2-ctl -d /dev/video-camera1 \
  --set-fmt-video=width=2112,height=1568,pixelformat=NV12 \
  --stream-mmap=4 --stream-count=300 --stream-poll >/tmp/j8.log 2>&1 &
wait
```

结果：

```text
J7=0 J8=0
J7: 30.05 FPS
J8: 30.05 FPS，启动时 dropped buffers: 1
```

两路均完成 `stream ON/OFF`，没有持续 overflow、IOMMU fault 或 timeout。J8 启动时一次
`not active buffer`/丢一帧属于启动阶段现象，未影响持续采集。

## 6. 三套 Demo 的扩展和部署

项目：

```text
/home/wx/2-demoRK/zx-camera-minimal
/home/wx/2-demoRK/zx-camera-web-demo
/home/wx/2-demoRK/zx-camera-qt-demo
```

板端部署目录：

```text
/home/zxwy/zx-camera-minimal
/home/zxwy/zx-camera-web-demo
/home/zxwy/zx-camera-qt-demo
```

重新烧写后 SSH 主机密钥发生变化，本次确认的新 ED25519 指纹为：

```text
SHA256:Krr8mQXSqaYbAisLVEKIv+dOfRbKDk02ZuKoMabBPgA
```

测试阶段使用临时 known-hosts 策略，没有覆盖用户长期保存的 SSH 主机记录。

### 6.1 Minimal

- 默认 OV 节点改为 `/dev/video-camera0`；
- 新增 `scripts/run_all_cameras.sh`；
- UC325 通过 sysfs 和 V4L2 capture capability 自动发现；
- 本机 UC325 Capture 节点为 `/dev/video40`，`video41` 不是采集节点。

三路同时采集结果：

```text
J7:    90 帧，28.64 FPS，无帧号间断
J8:    90 帧，28.32 FPS，启动阶段一次间断
UC325: 90 帧，30.99 FPS，无帧号间断
```

Minimal 的用途是无 GUI、无浏览器、无 JPEG 重编码的 V4L2/MMAP 基线测试。出现故障时，
先用它区分摄像头/驱动问题与 Qt/Web 控制逻辑问题。

### 6.2 Web

Web Demo 从单路 UC325 扩展为：

- `/j7.mjpg`：J7 OV13850；
- `/j8.mjpg`：J8 OV13850；
- `/uc325.mjpg`：UC325；
- `/`：三路组合页面；
- `/status`：运行状态。

两路 OV13850 使用 multi-planar NV12，当前由 CPU 转 RGB 并通过 libjpeg 编码；UC325 的
MJPEG 直接透传。OV 默认每三帧编码一帧，采集约 30 FPS，Web 输出约 10 FPS。

安装依赖时曾因 `v4l-utils` 被 hold，整条带 `-y` 的 apt 命令被拒绝，导致
`jpeglib.h` 没有安装，Web 编译报错。处理原则是先查看 hold 状态，只安装缺失开发包：

```bash
apt-mark showhold
sudo apt install libjpeg-dev
```

不要为了编译 Web Demo 强制升级被 BSP 固定的 `v4l-utils`。

Web 测试中曾观察到 J7 黑屏、J8 和 UC325 正常。该现象还需用 test pattern、曝光值和
保存单帧进行区分；不能仅凭黑色页面认定 J7 没有出帧，因为 J7 的 30 FPS 采集已经通过。

### 6.3 Qt

Qt Demo 扩展为 J7、J8、UC325 三个面板，每路独立线程，支持单路 Start/Stop 和
Start All/Stop All。UC325 节点自动发现。

第一版问题：线程处于 Waiting 状态时 `running_` 仍为真，重复 Start 无法真正重启。
第一轮修复增加了超时恢复和重复 Start 复位，但多次 Stop/Start 后仍可能让两路 OV13850
停在 `Waiting for frame`。

最终控制策略改为“持久采集状态机”：

- 第一次 Start 执行 open、配置、申请 buffer 和 STREAMON；
- Stop 只暂停 UI 更新，后台仍持续 DQBUF/QBUF；
- 再次 Start 只恢复显示，不再重复 STREAMOFF/S_FMT/STREAMON；
- 关闭窗口时才真正 shutdown 并释放 V4L2 设备；
- 连续三次意外 poll timeout 时保留恢复机制。

新版本已在板端成功构建。该方案针对 RKISP/RKAIQ 多次快速切流容易残留状态的问题。
需要注意：Qt 显示 `Paused (capture active)` 时仍然占用 mainpath；要切换到 Web、Cheese
或其他进程，必须关闭 Qt 程序，而不只是按 Stop。

## 7. NPU 环境初查

板端未出现 `/dev/rknpu*`，但日志明确显示：

```text
RKNPU: rknpu iommu is enabled, using iommu mode
[drm] Initialized rknpu 0.9.8 20240828 for fdab0000.npu on minor 1
```

运行库：

```text
/usr/lib/librknnrt.so
/usr/lib/aarch64-linux-gnu/librga.so.2.1.0
```

结论是驱动和 Runtime 基础已具备，`/dev/rknpu*` 不存在本身不是故障；下一步应确认
`/dev/dri`、开发头文件，并运行真实 `.rknn` 模型。

SDK 已找到可用的初始测试资产：

```text
debian/overlay-debug/usr/bin/rknn_demo
debian/overlay-debug/usr/share/rknn_demo/mobilenet_ssd.rknn
debian/overlay-debug/usr/share/rknn_demo/coco_labels_list.txt
debian/overlay-debug/usr/share/rknn_demo/box_priors.txt
```

NPU 日志中的 `failed to find power_model node` 和动态系数缺失暂不阻塞功能冒烟测试，
但在进行持续性能、温控和功耗测试前，应对照板级 DTS 修复。

## 8. 今日结论与遗留项

已确认：

- J7、J8 两套硬件链路均启用且可以同时稳定采集；
- 两路 mainpath 稳定别名正确；
- RAW busy 是 RKAIQ 正常占用，不应停止 3A 服务来绕开；
- minimal 三路并发通过；
- Web、Qt 已扩展为三路；
- Qt 反复切流的根因位于生命周期/状态控制，已改为持久采集；
- NPU 驱动和运行库已经存在。

尚需验证：

1. 在真实 GUI 操作下对最新版 Qt 连续 Start/Stop 50 次并运行 30 分钟；
2. Web 的 J7 黑屏是否随最新版采集控制消失，否则检查 test pattern、曝光和 NV12 单帧；
3. 板端 `/dev/dri`、`rknn_api.h`、RGA 开发头文件；
4. 使用 SDK 自带 `mobilenet_ssd.rknn` 完成首次 NPU 推理；
5. 推理稳定后再把 RGA/RKNN 接入三路摄像头应用。

