# J7 OV13850 + YOLOv5 RKNN 实时推理准备与版本匹配指南

日期：2026-08-05  
目标平台：ZX2603-2 / RK3588  
摄像头：J7 OV13850  
采集节点：`/dev/video-camera0`

## 1. 目标与实施边界

第一阶段只实现以下闭环：

```text
J7 OV13850
    ↓ V4L2 / NV12 / 2112×1568
最新帧槽（深度 1）
    ↓ NV12→RGB、resize、letterbox
YOLOv5n 640×640 RKNN
    ↓ RK3588 NPU
解码、置信度过滤、NMS
    ↓
终端输出检测结果和性能
```

第一阶段不同时接入 J8、UC325、Qt 和 Web。这样可以分别验证摄像头、预处理、
RKNN Runtime、模型输出和后处理。无界面版本稳定后，再向现有 Qt/Web Demo 集成。

## 2. 已确认的基础环境

### 2.1 摄像头

- J7 稳定节点：`/dev/video-camera0`；
- ISP mainpath：`rkisp0-vir2`；
- 输出格式：NV12；
- 当前分辨率：2112×1568；
- 已验证帧率：约 30.05 FPS；
- RAW 节点 `/dev/video0` 由 `rkaiq_3A_server` 使用，应用不能占用该节点。

### 2.2 NPU 与图像加速

- 内核：Linux 5.10.160；
- RKNPU 驱动日志版本：`0.9.8 20240828`；
- RKNPU 已通过 DRM 初始化；
- IOMMU 已启用；
- RKNN Runtime：`/usr/lib/librknnrt.so`；
- RGA Runtime：`/usr/lib/aarch64-linux-gnu/librga.so.2.1.0`。

没有 `/dev/rknpu*` 不能单独作为故障依据，这版驱动通常通过 `/dev/dri` 节点提供接口。

当前仍需确认：

- `/dev/dri/card*`、`/dev/dri/renderD*`；
- `rknn_api.h`；
- `im2d.h`、`RgaApi.h`；
- `librknnrt.so` 的真实 API/Runtime 版本；
- SDK 自带 `.rknn` 模型能否实际运行。

## 3. PC 和板端分别准备什么

## 3.1 x86_64 PC

PC 负责模型训练、导出和转换，需要：

- x86_64 Ubuntu；
- Python 虚拟环境；
- 与板端 Runtime 配套的 RKNN-Toolkit2；
- PyTorch、ONNX 和模型导出依赖；
- YOLOv5 官方或 Rockchip 适配代码；
- 量化校准图片；
- 可复现的版本清单。

建议第一版使用：

- 模型：YOLOv5n；
- 输入：640×640；
- batch：1；
- 输入颜色：RGB；
- 固定输入尺寸；
- 先验证非量化模型，再生成 INT8 模型。

## 3.2 RK3588 板端

板端负责采集、预处理、推理和结果显示，需要：

- V4L2、RKISP、RKAIQ；
- `librknnrt.so` 与匹配的 `rknn_api.h`；
- `librga.so` 与匹配的 RGA 头文件；
- CMake、GCC/G++；
- `.rknn` 模型和类别标签；
- 由同一 RKNN 示例提供的后处理代码。

基础构建工具：

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config v4l-utils
```

RKNN/RGA 开发文件优先从当前 BSP/SDK 或与当前运行库配套的官方 Runtime 包中获取，
不要只通过 Ubuntu 通用仓库安装一个不确定版本。

## 4. 版本匹配原则

RKNN 链路不是单个版本，而是以下组件共同组成：

```text
YOLOv5导出代码 / ONNX
        ↓
RKNN-Toolkit2（PC）
        ↓ 生成 .rknn
RKNN Runtime + rknn_api.h（板端）
        ↓
RKNPU内核驱动
```

RGA 是预处理链路，需另外保证其头文件与动态库兼容。

### 4.1 最重要的规则

1. `rknn_api.h` 和 `librknnrt.so` 必须来自同一套 Runtime 发布包。
2. RKNN-Toolkit2 应与板端 Runtime 属于同一发布系列，优先完全同版。
3. 不要用新版 Toolkit2 生成模型，再交给明显更旧的 Runtime 直接运行。
4. 不要只替换 `librknnrt.so`，而保留旧头文件、旧示例和旧模型。
5. YOLOv5 后处理必须与导出模型的输出节点、anchors、stride、量化方式一致。
6. RGA 的 `im2d.h`、`RgaApi.h` 和 `librga.so` 应来自同一 librga 版本。
7. NPU 驱动通常由 BSP 内核决定，不建议为适配用户态 Runtime 单独替换内核模块。

### 4.2 当前板卡的保守策略

当前只能确认驱动版本为 `0.9.8 20240828`，尚未取得 Runtime 的准确版本号。因此：

1. 先运行当前 SDK/rootfs 自带的 RKNN Demo 和模型；
2. 从 Demo 输出或 `rknn_query(RKNN_QUERY_SDK_VERSION)` 读取版本；
3. 以板端 Runtime 版本为基准选择 PC 端 Toolkit2；
4. 建立一个冻结版本目录，不再混入其他 SDK 的 RKNN 文件；
5. 新版本升级必须成套替换并保留旧版本回滚包。

### 4.3 运行时读取版本

程序应在初始化后查询版本：

```cpp
rknn_sdk_version version{};
int ret = rknn_query(ctx, RKNN_QUERY_SDK_VERSION,
                     &version, sizeof(version));
if (ret == RKNN_SUCC) {
    printf("RKNN API: %s\n", version.api_version);
    printf("RKNPU driver: %s\n", version.drv_version);
}
```

这比根据 `.so` 文件名推测版本可靠。每次程序启动都应将这两个版本写入日志。

### 4.4 建议建立版本清单

在 PC 和板端分别保存：

```bash
sha256sum /usr/lib/librknnrt.so
sha256sum /usr/lib/aarch64-linux-gnu/librga.so.2.1.0
uname -a
cat /proc/device-tree/compatible | tr '\0' '\n'
dmesg | grep -i rknpu
```

PC 端保存：

```bash
python3 -m pip freeze > requirements-lock.txt
python3 -c 'import rknn; print(rknn.__file__)'
sha256sum yolov5n.onnx yolov5n_640.rknn
git -C yolov5 rev-parse HEAD
```

建议随模型保存一个清单：

```text
model-name: yolov5n_640_int8
soc: rk3588
toolkit2-version: 待填写
runtime-api-version: 待填写
rknpu-driver-version: 0.9.8 20240828
onnx-sha256: 待填写
rknn-sha256: 待填写
input-shape: 1x640x640x3
input-layout: NHWC
input-type: UINT8
color-order: RGB
letterbox-value: 114
labels-version: 待填写
postprocess-version: 待填写
```

## 5. 阶段一：板端环境检查

```bash
ls -l /dev/dri

find /usr/include /usr/local/include -type f \
  \( -name rknn_api.h -o -name im2d.h -o -name RgaApi.h \) \
  2>/dev/null

ldconfig -p | grep -E 'rknn|rga'
ldd /usr/lib/librknnrt.so

command -v rknn_demo rknn_common_test
find /usr/share -iname '*.rknn' 2>/dev/null
```

普通用户运行时还要检查 DRM 节点权限：

```bash
id
getfacl /dev/dri/renderD* 2>/dev/null
```

如果没有 `rknn_api.h`，当前环境只能运行已有二进制，不能编译自研 RKNN 程序。

## 6. 阶段二：官方模型冒烟测试

当前本地 SDK 中已经找到：

```text
/home/wx/1-rk3588-sdk/rk3588_sdk/debian/overlay-debug/usr/bin/rknn_demo
/home/wx/1-rk3588-sdk/rk3588_sdk/debian/overlay-debug/usr/share/rknn_demo/mobilenet_ssd.rknn
/home/wx/1-rk3588-sdk/rk3588_sdk/debian/overlay-debug/usr/share/rknn_demo/coco_labels_list.txt
/home/wx/1-rk3588-sdk/rk3588_sdk/debian/overlay-debug/usr/share/rknn_demo/box_priors.txt
```

应优先让该二进制、模型、标签和配置保持同套部署。运行前检查：

```bash
ldd /usr/bin/rknn_demo
```

运行时另开终端监控：

```bash
dmesg -w | grep -iE 'rknpu|iommu|fault|reset|timeout'
```

通过标准：

- RKNN context 初始化成功；
- 输入输出 tensor 查询成功；
- 推理和结果获取成功；
- 连续运行至少 100 次；
- 没有 IOMMU fault、NPU reset 或 job timeout。

如果 SDK Demo 依赖桌面或特定摄像头，应改用 `rknn_common_test` 或官方单图示例，
不要将显示问题误判为 NPU 故障。

## 7. 阶段三：准备 YOLOv5 模型

### 7.1 导出 ONNX

示例：

```bash
python export.py \
  --weights yolov5n.pt \
  --include onnx \
  --img 640 640 \
  --batch 1 \
  --opset 12 \
  --simplify
```

实际参数应以所选 Rockchip YOLOv5 示例支持的导出方式为准。重点记录：

- YOLOv5 Git commit；
- PyTorch、ONNX、ONNX Runtime 版本；
- opset；
- 是否包含 NMS；
- 输出节点数量、形状和顺序。

优先使用“不在模型内包含 NMS”的普通检测头，由板端 C++ 完成解码和 NMS，便于控制。

### 7.2 转换 RKNN

目标平台必须指定 RK3588：

```python
rknn.config(
    target_platform='rk3588',
    mean_values=[[0, 0, 0]],
    std_values=[[255, 255, 255]],
)
```

先生成非量化模型验证结构和后处理：

```python
rknn.build(do_quantization=False)
```

再生成 INT8 模型：

```python
rknn.build(
    do_quantization=True,
    dataset='dataset.txt',
)
```

量化集建议准备 100～300 张 J7 实际画面，覆盖明暗、室内外、目标距离和真实背景。
量化图片不得全部来自互联网通用图片，否则真实摄像头画面的精度可能明显下降。

## 8. 阶段四：J7 静态帧验证

先保存一帧 NV12：

```bash
fuser -v /dev/video-camera0

v4l2-ctl -d /dev/video-camera0 \
  --set-fmt-video=width=2112,height=1568,pixelformat=NV12 \
  --stream-mmap=4 --stream-count=1 \
  --stream-to=/tmp/j7_2112x1568_nv12.yuv

stat -c '%s' /tmp/j7_2112x1568_nv12.yuv
```

预期大小：

```text
2112 × 1568 × 3 / 2 = 4,967,424 bytes
```

静态验证流程：

```text
NV12 → RGB → 等比例缩放 → 640×640填边 → RKNN → YOLOv5后处理
```

必须确认：

- RGB/BGR 顺序；
- 输入 tensor 是 NHWC 还是 NCHW；
- UINT8、INT8 或 FP16；
- mean/std 是否已由 Runtime 处理；
- letterbox 填充值，通常为 114；
- anchors、stride、输出顺序与模型一致；
- 量化输出的 scale、zero-point 使用正确；
- 检测框能正确映射回 2112×1568。

静态图应同时在 PC ONNX 和板端 RKNN 上测试，比较类别、置信度和框位置。

## 9. 阶段五：J7 实时无界面推理

建议新增独立项目：

```text
zx-camera-yolov5-demo/
├── CMakeLists.txt
├── include/
│   ├── v4l2_camera.h
│   ├── rknn_detector.h
│   └── detection.h
├── src/
│   ├── main.cpp
│   ├── v4l2_camera.cpp
│   ├── preprocess.cpp
│   ├── rknn_detector.cpp
│   └── postprocess.cpp
├── models/
│   ├── yolov5n_640.rknn
│   ├── labels.txt
│   └── model-manifest.txt
└── scripts/run_j7.sh
```

线程模型：

```text
V4L2采集线程 → 最新帧槽（深度1） → 单个RKNN推理线程 → 结果槽
```

要求：

- J7 持续 STREAMON；
- 采集线程不等待 NPU；
- 推理只取最新帧；
- 旧帧直接覆盖，禁止无限队列；
- 第一版使用一个 RKNN context；
- 先用 CPU 预处理验证，再切换 RGA；
- 每秒输出采集 FPS、推理 FPS 和各阶段耗时。

运行前必须关闭会占用 J7 的 Qt、Web、Cheese：

```bash
fuser -v /dev/video-camera0
```

现有 Qt 采用持久采集，按 Stop 可能仍然持有设备；测试 AI Demo 时应完全退出 Qt。

## 10. 预处理与后处理要求

### 10.1 预处理

第一版可以用 CPU 完成 NV12→RGB、resize 和 letterbox。结果正确后再用 RGA 优化。

如果模型转换配置已定义 mean/std，板端通常输入原始 UINT8 RGB，不能再次手动除以 255，
否则会重复归一化。以 `rknn_query()` 返回的输入属性和转换脚本为准。

### 10.2 坐标还原

```text
scale = min(640 / 2112, 640 / 1568)
new_w = round(2112 × scale)
new_h = round(1568 × scale)
pad_x = (640 - new_w) / 2
pad_y = (640 - new_h) / 2

original_x = (model_x - pad_x) / scale
original_y = (model_y - pad_y) / scale
```

还原后将坐标裁剪到原始图像范围。

## 11. 编译链接示例

假定匹配的头文件已经部署：

```cmake
find_package(Threads REQUIRED)

add_executable(zx-camera-yolov5
    src/main.cpp
    src/v4l2_camera.cpp
    src/preprocess.cpp
    src/rknn_detector.cpp
    src/postprocess.cpp
)

target_include_directories(zx-camera-yolov5 PRIVATE
    include
    /usr/include/rknn
    /usr/include/rga
)

target_link_libraries(zx-camera-yolov5 PRIVATE
    rknnrt
    rga
    Threads::Threads
)
```

构建：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

运行接口建议：

```bash
./build/zx-camera-yolov5 \
  --device /dev/video-camera0 \
  --width 2112 --height 1568 --format NV12 \
  --model models/yolov5n_640.rknn \
  --labels models/labels.txt
```

## 12. 验收标准

### 12.1 功能

- J7 画面颜色正常；
- 类别、置信度和检测框正确；
- PC ONNX 与板端 RKNN 输出基本一致；
- 无目标时没有大量固定位置误报。

### 12.2 性能

程序每秒至少输出：

```text
capture_fps
inference_fps
preprocess_ms
rknn_run_ms
postprocess_ms
dropped_frames
detections
```

第一阶段建议目标：

- J7 采集保持约 30 FPS；
- 检测至少 10 FPS；
- 端到端延迟不持续增长；
- 连续运行 30 分钟无 timeout；
- 后续完成 4～8 小时稳定性测试。

### 12.3 内核日志

测试期间不能持续出现：

- RKNPU job timeout；
- IOMMU fault；
- NPU reset；
- RKCIF overflow；
- RKISP 无法取得 buffer；
- J7 `select timeout`。

## 13. 常见版本不匹配表现

| 现象 | 优先怀疑 |
|---|---|
| `rknn_init` 返回模型无效或版本错误 | Toolkit2 生成版本高于板端 Runtime，或目标平台错误 |
| 程序启动即符号找不到 | `rknn_api.h` 与 `librknnrt.so` 不同版 |
| 推理成功但所有结果错误 | 输出顺序、量化参数或后处理与模型不匹配 |
| PC 正常、板端框严重偏移 | letterbox/坐标还原、RGB/BGR 或输入布局错误 |
| INT8 精度显著下降 | 量化集不匹配、预处理重复归一化 |
| RGA 返回不支持或颜色异常 | RGA 头文件/库不匹配或 stride/format 配置错误 |
| NPU timeout/IOMMU fault | Runtime/驱动兼容、buffer 地址或设备树问题 |

出现问题时按以下顺序回退：

1. SDK 自带二进制 + 自带模型；
2. 自己编译的官方单图示例 + 自带模型；
3. 自己转换的非量化 YOLOv5；
4. 自己转换的 INT8 YOLOv5；
5. J7 静态帧；
6. J7 实时流；
7. Qt/Web 集成。

## 14. 下一步实际操作清单

1. 在板端执行环境检查命令并记录 `/dev/dri`、头文件和 RKNN Demo 状态。
2. 运行 SDK 自带 RKNN 模型并读取 API/驱动版本。
3. 在 PC 建立冻结的 Toolkit2 Python 虚拟环境。
4. 选择与该 Runtime 匹配的 Rockchip YOLOv5 示例。
5. 导出 YOLOv5n 640×640 ONNX。
6. 先生成非量化 RKNN，再生成 INT8 RKNN。
7. 保存 J7 静态 NV12，完成 PC/板端结果对比。
8. 编写无界面 J7 实时推理 Demo。
9. 连续稳定运行后，再接入 Qt 和 Web。

