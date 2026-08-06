# RK3588 三路摄像头实时目标识别下一步操作建议

日期：2026-08-04  
目标板：ZX2603-2 / RK3588，地址 `192.168.10.65`

## 1. 当前环境判断

| 项目 | 当前状态 | 结论 |
|---|---|---|
| RKNPU 内核驱动 | `rknpu 0.9.8 20240828` 已通过 DRM 初始化 | 基本正常 |
| IOMMU | 日志显示 `rknpu iommu is enabled` | 正常 |
| RKNN Runtime | `/usr/lib/librknnrt.so` 存在且已进入动态链接缓存 | 已具备运行基础 |
| RGA Runtime | `librga.so.2.1.0` 存在 | 已具备运行基础 |
| `/dev/rknpu*` | 不存在 | 不能据此判定故障；该驱动通常通过 DRM 节点工作 |
| RKNN/RGA 开发头文件 | 板端尚未确认；SDK 中已找到 RGA 头文件 | 编译自研程序前必须补查 |
| 实际模型推理 | 尚未执行 | 当前最关键的待验证项 |

日志已经出现：

```text
[drm] Initialized rknpu 0.9.8 20240828 for fdab0000.npu on minor 1
```

因此 NPU 驱动已经完成注册。`failed to find power_model node` 和
`failed to get dynamic-coefficient` 主要表示设备树缺少 NPU 功耗模型参数，可能影响
功耗估算、温控或动态调频策略，但不等价于 NPU 无法推理。应先完成模型冒烟测试，
再处理功耗模型。

`can't request region` 后驱动仍然完成 DRM 初始化，现阶段作为警告记录；如果后续出现
推理失败、IOMMU fault 或 NPU reset，再对照原厂设备树排查资源重复声明。

## 2. 第一步：补齐板端只读检查

在板端执行：

```bash
ls -l /dev/dri
for n in /sys/class/drm/card* /sys/class/drm/renderD*; do
    [ -e "$n" ] || continue
    echo "===== $n ====="
    readlink -f "$n/device/driver"
done

find /usr/include /usr/local/include -type f \
    \( -name rknn_api.h -o -name im2d.h -o -name RgaApi.h \) 2>/dev/null

ldd /usr/lib/librknnrt.so
ls -l /usr/lib/librknnrt.so /usr/lib/aarch64-linux-gnu/librga.so*
```

预期至少出现 `/dev/dri/card*` 或 `/dev/dri/renderD*`。普通用户运行时还要确认节点权限：

```bash
id
getfacl /dev/dri/renderD* 2>/dev/null
```

如果 `rknn_api.h` 不存在，说明目前只有运行库，不能直接编译 RKNN C/C++ 程序。
这时应从与当前 `librknnrt.so` 配套的 RKNN Runtime/SDK 中安装头文件和链接库，
不要随意混用不同版本的头文件、运行库与模型工具链。

## 3. 第二步：先跑 SDK 自带 RKNN 冒烟测试

当前 SDK 已包含以下资产：

```text
/home/wx/1-rk3588-sdk/rk3588_sdk/debian/overlay-debug/usr/bin/rknn_demo
/home/wx/1-rk3588-sdk/rk3588_sdk/debian/overlay-debug/usr/share/rknn_demo/mobilenet_ssd.rknn
/home/wx/1-rk3588-sdk/rk3588_sdk/debian/overlay-debug/usr/share/rknn_demo/coco_labels_list.txt
/home/wx/1-rk3588-sdk/rk3588_sdk/debian/overlay-debug/usr/share/rknn_demo/box_priors.txt
```

先检查板端是否已有这些文件：

```bash
command -v rknn_demo
find /usr/share -maxdepth 3 -iname '*.rknn' 2>/dev/null
```

若没有，可将上述四个 SDK 文件按原目录复制到板端，然后执行：

```bash
ldd /usr/bin/rknn_demo
/usr/bin/rknn_demo
dmesg | tail -n 100
```

冒烟测试的通过标准是：

- `rknn_init`、输入输出查询和 `rknn_run` 不报错；
- 能得到稳定的分类或检测结果；
- `dmesg` 中没有 IOMMU fault、NPU reset、job timeout；
- 连续运行 100 次无资源泄漏或崩溃。

若自带程序依赖显示器或摄像头，不适合作为纯推理测试，可进一步寻找并运行 SDK 的
`rknn_common_test`。不要在尚未验证运行时兼容性前直接把 AI 逻辑塞入 Qt/Web 主程序。

## 4. PC 与板端的职责划分

### PC：模型转换和量化

在 x86_64 Ubuntu 主机安装与板端 Runtime 配套的 RKNN-Toolkit2：

1. 选择轻量检测模型，如 YOLOv5n、YOLOv8n 或厂商已验证模型；
2. 将 ONNX 转换为 RK3588 `.rknn`；
3. 优先做 INT8 量化；
4. 量化数据集应包含 J7、J8、UC325 三路真实画面和不同光照；
5. 在 Toolkit2 模拟器与板端分别验证输出一致性。

### RK3588：采集、预处理、推理和显示

板端程序链接：

- `librknnrt`：NPU 推理；
- `librga`：NV12/RGB 转换、缩放和 letterbox；
- V4L2：摄像头采集；
- Qt 或 Web：结果显示。

板端不需要安装 Python Toolkit2 来做模型转换。板端开发只需要匹配的 Runtime、头文件、
RGA 开发文件和 C/C++ 构建环境。

## 5. 推荐的软件结构

不要让采集线程等待 NPU，也不要为每个显示帧都排队推理。建议结构如下：

```text
J7 V4L2 ─┐
J8 V4L2 ─┼─> 各路最新帧槽（深度 1） ─> RGA 预处理 ─> RKNN 调度器
UC325 ───┘                                      │
                                                ├─> 检测框/类别/置信度
                                                ├─> Qt 叠加显示
                                                └─> Web JPEG + JSON
```

关键约束：

- 延续当前 Qt 的“持久采集”模式；暂停预览时不反复 `STREAMOFF/S_FMT/STREAMON`；
- 每路只保留最新帧，旧帧直接覆盖，避免延迟不断累积；
- 第一阶段使用一个推理线程轮询三路，不要一开始同时创建三个 RKNN context；
- 显示可保持 25～30 FPS，检测结果允许每路 5～15 FPS；
- 保存 resize/letterbox 参数，后处理时将检测框准确映射回原图；
- 推理稳定后再评估多 context、异步执行和 NPU core mask。

## 6. 推荐实施顺序

1. 跑通单张图片的 RKNN 官方示例。
2. 用 J7 保存的一帧 NV12 做离线推理，验证颜色、尺寸和检测框。
3. 将推理接入 minimal，只接 J7，做无界面的 FPS/延迟统计。
4. 加入 J8，再加入 UC325，采用单推理线程轮询。
5. 接入 Qt，只把结果通过信号传回 UI 线程绘制。
6. 接入 Web，提供带框 MJPEG，并增加 `/detections.json`。
7. 连续运行 30 分钟，再做 4～8 小时稳定性测试。
8. 最后优化 RGA 零拷贝、NPU 核心调度和硬件 JPEG 编码。

建议新增模块：

```text
zx-camera-ai-demo/
├── include/inference/rknn_detector.h
├── include/inference/detection.h
├── src/inference/rknn_detector.cpp
├── src/inference/preprocess_rga.cpp
├── src/inference/postprocess.cpp
├── models/
└── tests/
```

## 7. 性能与稳定性验收

每次测试至少记录：

- 三路采集 FPS、丢帧数和 V4L2 timeout；
- 每路预处理、推理、后处理耗时；
- 端到端画面延迟；
- CPU、NPU、内存占用和 SoC 温度；
- `dmesg` 中的 RKNPU、IOMMU、RKCIF、RKISP 错误；
- 连续 Start/Stop 或页面刷新后是否仍有帧；
- Qt 暂停时设备仍被占用这一预期行为。

建议先以“3 路采集均为 30 FPS、检测每路至少 8 FPS、队列不增长、30 分钟无超时”
作为第一阶段目标，而不是立即追求三路各 30 FPS 推理。

## 8. 功耗模型的后续修复

完成真实模型推理后，再比较当前板级 DTS 与能正常工作的厂商 RK3588 DTS 中以下内容：

- NPU OPP 表；
- `power_model` 或动态功耗系数；
- `rknpu-supply`、`mem-supply`；
- NPU devfreq、温控 cooling-map；
- NPU 三个核心的寄存器资源声明。

该修复需要与当前 PMIC、电压表和散热设计匹配，不能直接照抄另一块板子的电压/功耗参数。

