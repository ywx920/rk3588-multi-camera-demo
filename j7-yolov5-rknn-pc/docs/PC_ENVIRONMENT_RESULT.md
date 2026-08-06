# J7 YOLOv5 RKNN Linux PC 环境配置结果

日期：2026-08-05  
工作目录：`/home/wx/2-demoRK/j7-yolov5-rknn-pc`

## 1. 主机检查结果

| 项目 | 结果 |
|---|---|
| 操作系统 | Ubuntu 24.04.4 LTS |
| 内核 | Linux 7.0.0-28-generic |
| 架构 | x86_64 |
| CPU | 16 个逻辑核 |
| 内存 | 13 GiB，总可用约 12 GiB |
| Swap | 4 GiB |
| 工作盘可用空间 | 检查时约 218 GiB |
| NVIDIA GPU | 未检测到；RKNN 模型转换不要求 NVIDIA GPU |
| 系统 Python | 3.12.3，不适配 RKNN-Toolkit2 1.5.2 wheel |
| Git | 2.43.0 |
| CMake | 3.28.3 |

主机资源满足 J7 YOLOv5 模型导出、RKNN 转换和 PC 模拟器验证要求。

## 2. 版本选择依据

现有 RK3588 BSP 文档包含：

```text
RKNNToolKit2_OP_Support-1.5.2.md
changelog-1.5.2.txt
```

并明确说明 rknpu2 应与 RKNN-Toolkit2 1.5.2 同步。因此 PC 端没有安装当前最新版，
而是锁定 Rockchip 官方 `v1.5.2` 标签。

该标签提供以下 x86_64 wheel：

```text
cp36
cp38
cp310
```

本机系统只有 Python 3.12，因此在项目内安装 Python 3.10.20，并使用 cp310 wheel。
系统 Python 未被替换或修改。

## 3. 已安装环境

| 组件 | 版本 |
|---|---|
| 项目内 Python | 3.10.20 |
| uv | 0.12.1 |
| RKNN-Toolkit2 | 1.5.2+b642f30c |
| NumPy | 1.23.4 |
| ONNX | 1.14.0 |
| ONNX Runtime | 1.14.1 |
| OpenCV Python | 4.5.5.64 |
| SciPy | 1.9.3 |
| Protobuf | 3.20.3 |
| Torch | 1.13.1+cpu |
| torchvision | 0.14.1+cpu |
| setuptools | 65.5.1 |

Torch 为 CPU 版本：

```text
torch.cuda.is_available() = False
```

这不影响 ONNX 导出和 RKNN 转换，只会使 PC 端 PyTorch 推理/训练比 GPU 慢。实际训练
建议在有 GPU 的工作站完成；本机适合模型导出、转换和功能验证。

## 4. 为什么没有安装 TensorFlow

Rockchip 的完整 `requirements_cp310-1.5.2.txt` 同时覆盖 ONNX、PyTorch 和 TensorFlow
三种模型前端，并会拉取数 GiB 的 CUDA/TensorFlow 依赖。当前目标只有 YOLOv5：

```text
PyTorch .pt → ONNX → RKNN
```

因此环境按用途拆分：

- ONNX→RKNN 的官方固定依赖已经安装；
- PyTorch/torchvision 使用同版本 CPU wheel；
- TensorFlow 未安装，因为当前流程不读取 `.pb`、SavedModel 或 TFLite。

若将来需要转换 TensorFlow 模型，应另建虚拟环境，不建议直接修改当前已验证环境。

## 5. 已下载内容

```text
downloads/rknn-toolkit2-v1.5.2.tar.gz
downloads/yolov5-v6.2.tar.gz
third_party/rknn-toolkit2/
third_party/yolov5/
```

Rockchip Toolkit 来源：官方 GitHub `rockchip-linux/rknn-toolkit2` 的 `v1.5.2` 标签。  
YOLOv5 来源：Ultralytics 官方 GitHub 的 `v6.2` 标签。

注意：用于 RKNN 验证的模型是 Rockchip 示例内置的 `yolov5s_relu.onnx`。普通
Ultralytics YOLOv5 使用 SiLU，不能因为同叫 YOLOv5 就直接替换而省略兼容性验证。
自定义模型应按照 Rockchip 模型动物园对应导出方法处理，并保持输出头和后处理一致。

## 6. 文件校验值

```text
2733d612bafff4f8450815b83b5ef9bd58d86e1e590a1dd786f67f0e66ba00ce
  downloads/rknn-toolkit2-v1.5.2.tar.gz

3556ed9e072d3466ed3bd91c414859627c92f4715f4dc87be98fdf1284ab1e91
  downloads/yolov5-v6.2.tar.gz

4deb4a2079d617c2adac004f045a14a250340259d8bfe0f758f63d1dd5dfbaa2
  third_party/rknn-toolkit2/packages/
  rknn_toolkit2-1.5.2+b642f30c-cp310-cp310-linux_x86_64.whl

b532116d87b115490b2c5ff309ef5441e372877122eee7ef7f8c50079509beba
  models/source/yolov5s_relu_official.onnx

75f069f32269ef93afc23cbdc0410254e06c337f6be825743b393b568e8c08fd
  models/generated/yolov5s_relu_rk3588_int8.rknn

4b0c940ac5359efe4560abebd79f53722167196e63b31dc76c1d431f774b61d1
  models/generated/official_bus_result.jpg
```

## 7. 已完成的真实验证

不是只做了包导入检查，已经执行 Rockchip 官方 YOLOv5 ONNX 示例，并将
`target_platform` 从示例默认的 RK3566 改为 RK3588。

验证完成了：

1. 创建 RKNN 对象；
2. 加载 `yolov5s_relu.onnx`；
3. 使用示例校准集做 INT8 量化；
4. 编译 RK3588 RKNN 模型；
5. 导出 `.rknn`；
6. 启动 PC 模拟器；
7. 推理 `bus.jpg`；
8. 执行 YOLOv5 解码和 NMS；
9. 生成结果图。

检测结果包括：

```text
person: 0.8841
person: 0.8677
person: 0.8247
person: 0.3410
bus:    0.7022
```

生成文件：

```text
models/generated/yolov5s_relu_rk3588_int8.rknn
models/generated/official_bus_result.jpg
```

编译器报告：

```text
RKNN-Toolkit2: 1.5.2+b642f30c
librknnc:      1.5.2, build 2023-08-23
input dtype:   INT8
output dtype:  INT8
input layout:  NHWC（Python inference 默认）
target:        RK3588
```

## 8. 使用方法

### 8.1 激活环境

```bash
cd /home/wx/2-demoRK/j7-yolov5-rknn-pc
source scripts/activate.sh
```

### 8.2 检查环境

```bash
scripts/check_environment.sh
```

预期出现：

```text
RKNN import: OK
RKNN object: OK
RKNN-Toolkit2 version: 1.5.2+b642f30c
torch: 1.13.1+cpu
CUDA available: False
```

### 8.3 重新运行官方转换与模拟器测试

```bash
scripts/run_official_yolov5.sh
```

该命令会重新生成示例目录下的 `yolov5s_relu.rknn` 和 `result.jpg`。

## 9. 工作区结构

```text
j7-yolov5-rknn-pc/
├── .python/                 # 项目内 Python 3.10
├── .venv/                   # 已验证虚拟环境
├── .tools/uv-bin/           # uv 工具
├── config/                  # 固定依赖
├── datasets/calibration/    # 后续放 J7 量化图片
├── downloads/               # 官方下载归档
├── models/source/           # ONNX 输入模型
├── models/generated/        # RK3588 RKNN 和结果图
├── scripts/                 # 激活、检查和转换脚本
├── third_party/rknn-toolkit2/
└── third_party/yolov5/
```

空间占用约为：

```text
.venv                         1.4 GiB
.python                       88 MiB
third_party/rknn-toolkit2     473 MiB
third_party/yolov5            1.8 MiB
downloads                     367 MiB
models                        36 MiB
```

## 10. 当前版本边界

PC 端转换链路已验证，但板端 Runtime 的 API 版本仍需通过真实程序读取。目前板端已知：

```text
RKNPU driver: 0.9.8 20240828
librknnrt.so: 已安装，准确 API 版本待查询
```

在把生成模型作为正式模型前，应在板端执行 `RKNN_QUERY_SDK_VERSION`。如果板端 Runtime
不是 1.5.2 系列，先用 SDK 自带模型测试，再决定升级 Runtime 或调整 PC Toolkit，不能
直接覆盖板端 `librknnrt.so`。

## 11. 下一步

1. 将 `yolov5s_relu_rk3588_int8.rknn` 部署到板端。
2. 使用板端 RKNN C/C++ Demo 读取 API 和驱动版本。
3. 对该模型执行单图推理，确认 Runtime兼容。
4. 从 J7 保存 100～300 张真实图片制作量化集。
5. 用 J7 静态帧替换 `bus.jpg`，验证颜色、letterbox 和坐标映射。
6. 创建 J7 V4L2 + RKNN 无界面实时推理程序。
7. 稳定运行后再接入 Qt/Web。

