# 多路摄像头推理优化记录（2026-08-05）

## 优化前现象

- J7、J8、UC325三路同时预览和推理时CPU占用高、界面卡顿。
- 每路独占一个采集线程、一个推理线程和一个RKNN context。
- OV13850的2112×1568 NV12在CPU上逐像素转换成全分辨率RGB。
- 每帧包含MMAP到vector复制、全图复制、模型缩放和Qt二次缩放。

## 第一阶段：图像链路和帧率解耦

- 摄像头保持30 FPS采集。
- 默认显示上限15 FPS，推理提交上限8 FPS。
- 未参与处理的帧只执行DQBUF/QBUF，不复制图像数据。
- OV13850使用RGA一次完成NV12到RGB转换和缩放。
- 默认工作图宽度960，保持原图宽高比。
- 推理结果回调不再触发一次额外的全图刷新。
- RGA不可用时自动回退CPU转换，PC端仍可构建。

板端确认日志：

```text
NV12 conversion backend: RGA resize + color conversion
```

三路无界面压测时Qt进程约占一个CPU核心，整机约81%～84%空闲。

## 第二阶段：集中推理调度

- 所有摄像头改为共享`InferenceScheduler`。
- RKNN context数量由`--npu-workers`控制，范围1～3，默认3。
- 每个摄像头只有一个pending槽位；新帧覆盖旧帧，不形成延迟队列。
- 空闲worker按照round-robin顺序选择通道，避免高帧率通道长期抢占NPU。
- worker不再和摄像头固定绑定，因此扩展到8路时仍只创建最多3个context。
- 界面显示每路实际完成推理FPS和被覆盖的旧帧数量。

三路板端验证：

```text
Inference scheduler: 3 worker(s), 3 channel(s)
RKNN worker 0 initialized
RKNN worker 1 initialized
RKNN worker 2 initialized
```

三路真实摄像头能够同时采集、RGA预处理和RKNN推理，未发现RGA、RKNPU或
IOMMU fault。

## 参数

```bash
DISPLAY_FPS=15 INFERENCE_FPS=8 WORK_WIDTH=960 NPU_WORKERS=3 \
  bash scripts/run_board_all.sh
```

低负载/八路起步参数：

```bash
DISPLAY_FPS=10 INFERENCE_FPS=5 WORK_WIDTH=640 NPU_WORKERS=3 \
  bash scripts/run_board_all.sh
```

## 尚未完成的深度优化

- 已完成：摄像头改由JSON动态配置。
- 已完成：V4L2通过VIDIOC_EXPBUF导出DMA FD并直接交给RGA。
- RKNN输入改为预分配/零拷贝buffer，避免`rknn_inputs_set`复制。
- UC325的MJPEG软件解码改为MPP硬件JPEG解码。
- 新增配置文件动态描述4～8路设备，而不是在`main.cpp`中写死三路。
- 使用8路真实视频源做持续压力测试，验证USB带宽、ISP数量、内存带宽和温度。

## 第三阶段：动态配置、DMABUF和界面

- `config/cameras.json`成为摄像头列表的单一配置入口。
- `enabled:false`可以临时禁用通道，新增同结构对象即可扩展卡片。
- V4L2 MMAP缓冲通过`VIDIOC_EXPBUF`导出DMA FD。
- RGA直接读取DMA FD，处理完成后才将V4L2 buffer重新QBUF。
- 若驱动不支持EXPBUF，自动使用原有映射地址路径。
- Qt界面改为深色AI监控台样式，并根据通道数自动调整网格列数。

板端确认：

```text
NV12 conversion backend: V4L2 DMABUF -> RGA
Inference channel 0 first result
Inference channel 1 first result
Inference channel 2 first result
```

## 第四至第八阶段：交互稳定性和负载控制

- 固定摄像头卡片及画面尺寸，防止Pixmap和状态文本改变布局；
- 固定2×2布局：J7/J8顶部、UC325左下、右下预留；
- Stop后清空画面并丢弃迟到帧，解决随机残影；
- 摄像头软停止保持DQBUF/QBUF，避免反复STREAMOFF后`waiting for frame`；
- 增加逐路及全局推理开关；
- 推理结果增加轻量IoU跟踪、框平滑和超时清理；
- 空场景按2/3/4倍间隔自适应降频，重新检出目标后恢复配置频率；
- 增加后台启动、统一停止和稳定性监控脚本。

## 当前Qt有效基线

- 输入：J7 `/dev/video-camera0`、J8 `/dev/video-camera1`、UC325动态发现；
- OV13850：V4L2 DMABUF到RGA缩放和NV12转RGB；
- 推理：集中调度、最多3个worker、每通道只保留最新帧；
- UI：固定2×2、每路摄像头和推理独立开关、全局推理控制；
- Qt与Web独占相同V4L2节点，不能同时运行。

下一阶段不应继续增加UI补丁，优先完成端到端性能遥测、RKNN零拷贝输入、UC325
硬件解码，以及统一的长时间状态机压力测试。
