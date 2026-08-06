# RK3588多路视觉第五阶段：固定布局、检测平滑与稳定性监控

## 1. 用户现场现象

Qt窗口已经按屏幕尺寸缩放，但依次开启三路画面后，最后一张卡片仍可能被挤出
可视区域。问题不是摄像头分辨率本身，而是Qt布局的动态尺寸提示：

1. `QLabel`收到新Pixmap后会更新`sizeHint`；
2. NPU状态文本随推理结果变长；
3. 摄像头卡片和视口仅设置最小尺寸，仍允许布局扩张；
4. 网格重新分配空间后，第三列可能超过可视宽度，而水平滚动被关闭。

## 2. 固定布局修复

窗口创建时只计算一次布局尺寸：

- 根据主屏可用宽度选择1、2或3列；
- 窗口宽度上限为1500；
- 每张卡片宽度由内容宽度、列数和固定间距计算；
- 画面框固定为卡片宽度减22像素，高度固定为4:3并限制在190～560；
- `CameraPanel`与画面`QLabel`均使用固定宽度/尺寸；
- 实时图像始终按`KeepAspectRatio`缩入固定框，不再改变布局；
- 状态标签使用`Ignored`水平尺寸策略，长文本不能撑宽卡片；
- 小屏保留纵向滚动，三张卡片不会丢失。

采集分辨率仍为2112×1568或1280×720，只固定显示框，不改变RKISP和推理链路。

## 3. 推理显示优化

新增轻量IoU时序平滑：相邻推理结果中类别相同且IoU不小于0.25的目标框，使用
35%历史位置和65%最新位置进行指数平滑。它能减少低频推理时框体跳动，计算量
远低于再次执行模型。

这不是完整ByteTrack。完整ByteTrack还需要目标ID、卡尔曼预测、低置信度二次匹配
和轨迹生命周期管理，建议在定制模型类别与评价数据确定后作为独立模块接入。

某路关闭推理时，Qt不再无条件复制整张`QImage`；只有需要绘制检测框时才创建
绘制副本，纯预览直接复用不可变图像。

## 4. 24小时稳定性监控

新增脚本：

```text
zx-camera-qt-demo/scripts/monitor_stability.sh
zx-camera-qt-demo/scripts/start_stability_monitor.sh
zx-camera-qt-demo/scripts/stop_stability_monitor.sh
```

默认每10秒采集一次、运行24小时，记录进程PID、CPU、内存、RSS、线程数、最高
温度、NPU load（若内核节点存在）和Web摄像头状态。结束时保存最近500条摄像头、
RGA、RKNPU、IOMMU、timeout、fault及reset相关内核事件。

```bash
cd /home/zxwy/zx-camera-qt-demo
bash scripts/start_stability_monitor.sh
tail -f stability-monitor.log

# 两分钟快速验证
DURATION_SECONDS=120 INTERVAL_SECONDS=5 bash scripts/monitor_stability.sh
```

结果保存在`reports/stability-时间.csv`和对应的`events.log`。

## 5. 独立后续阶段

MPP H.264、RTSP/WebRTC会改变Web传输协议和前端播放方式，不能与布局修复混在
一次发布中。当前保留已验证的MJPEG链路，下一阶段应建立并行MPP原型，经单路和
三路压力测试后再替换现有Web输出。

RKNN预分配tensor memory也需确认当前`librknnrt.so`对应API和输入内存约束，后续
先做独立基准测试，再进入主程序，避免破坏三上下文调度。

## 6. 验收项

- 三个画面框在启动前后尺寸保持不变；
- 三路同时开启不会改变列宽或把第三路推出屏幕；
- 摄像头关闭仍持续回收底层缓冲；
- 推理关闭时预览正常且减少图像副本；
- 检测框跳动较上一版本减小；
- 稳定性脚本可以生成CSV和内核事件报告；
- PC源码编译通过，最终RKNN链接和运行在ARM64板端完成。

## 7. 板端验证结果

2026-08-06在板端完成Release原生编译。板卡当时停留在GDM登录界面，因此使用
`QT_QPA_PLATFORM=offscreen`运行12秒三路自动开启测试：三个RKNN worker全部初始化，
J7、J8、UC325三个通道均返回首个推理结果；OV13850路径确认继续使用
`V4L2 DMABUF -> RGA`。首批推理耗时约29～36 ms，未新增RGA、RKNPU或IOMMU fault。

稳定性监控使用1秒间隔完成4秒快速回归，CSV包含表头和3条采样数据，并正常生成
events日志。正式测试仍建议在图形桌面登录后运行24小时。
