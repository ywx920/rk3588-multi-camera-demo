# RK3588多路视觉第三阶段交付记录

## Qt

- JSON动态摄像头配置：`zx-camera-qt-demo/config/cameras.json`
- V4L2 EXPBUF导出DMA FD
- DMA FD直接送入RGA完成NV12转换与缩放
- 共享1～3个RKNN worker的公平推理调度
- 深色响应式摄像头卡片布局

三路板端实测均完成采集和推理，日志确认使用DMABUF到RGA路径，未出现RGA、
RKNPU或IOMMU fault。

## Web

- 使用同结构JSON动态生成摄像头卡片和MJPEG路由
- OV13850非编码帧取消整帧内存复制
- UC325按sysfs名称和V4L index自动定位
- 页面改为响应式深色监控台，包含在线状态和每路实时指标

板端HTTP验证结果：J7/J8约9.8 FPS（每3帧编码一次），UC325约30.5 FPS，
三个通道均无错误。

## 下一阶段

- Web服务复用RKNN集中调度器并在MJPEG中绘制检测框
- RKNN输入使用预分配tensor memory，减少输入复制
- 配置4～8路真实或网络视频源并进行温度、带宽和72小时稳定性测试
