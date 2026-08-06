# RK3588多路视觉第九阶段：MPP硬件MJPEG编码

> 2026-08-06勘误：本阶段最初只检查了JPEG头部，错误地把MPP输出判定为可用。
> 随后抓取完整帧发现码流存在DQT/SOS等解码错误，浏览器无法显示。当前程序已增加
> 首帧完整解码与尺寸校验；该板MPP 1.5.0的RGB888直编码校验失败，会安全回退到
> `Qt JPEG fallback`。下文原CPU数据来自损坏码流，不代表可用方案，不能作为优化结论。

## 1. 目标与技术选择

上一阶段Web三路有目标推理时进程CPU约128.7%，关闭推理后仍约40.8%，说明软件
JPEG编码是主要CPU消耗之一。

浏览器无法直接使用现有`<img>`标签播放裸H.264；直接切换H.264会破坏当前页面。
本阶段先使用Rockchip MPP硬件MJPEG编码替代Qt软件JPEG，保持HTTP MJPEG协议和
浏览器页面完全兼容。H.264/RTSP/WebRTC留作独立阶段。

板端已确认：

- `librockchip_mpp.so.1`；
- `librockchip-mpp-dev 1.5.0`；
- `mpi_enc_test`；
- RK3588 MPP JPEG编码能力。

## 2. 实现

新增`HardwareJpegEncoder`：

- 输入为叠加检测框后的Qt RGB888工作图；
- RGB行跨度按64字节对齐，高度按16行对齐；
- 使用MPP DRM buffer、`MPP_CTX_ENC`和`MPP_VIDEO_CodingMJPEG`；
- 质量参数为76；
- 合并MPP分区输出，并使用Qt完整解码首帧、校验宽高，而非只检查SOI；
- 初始化或编码失败时自动永久切换到Qt JPEG软件编码，不中断视频流。

Makefile通过`pkg-config rockchip_mpp`自动判断是否编译MPP路径。`/status`新增
`encoder`字段，页面显示以下后端之一：

- `MPP MJPEG`：MPP硬件编码；
- `MJPEG passthrough`：UC325关闭推理时原始MJPEG直通；
- `Qt JPEG fallback`：软件回退。

## 3. 板端复核与纠错

最初的验证结果如下，但后来确认不充分：

- J7、J8、UC325均报告`MPP MJPEG`；
- J7/J8约9.9 FPS，UC325约15.1 FPS；
- HTTP流包含正确multipart头；
- JPEG数据以`ff d8 ff e0 ... JFIF`开头；
- 当时仅检查了SOI/JFIF头，没有验证完整帧。

抓取完整multipart帧并使用FFmpeg解码后，出现JPEG量化表和扫描段错误，浏览器因此
不显示图像。加入完整首帧校验后，三路均自动使用`Qt JPEG fallback`，完整帧以
`ff d8`开始、`ff d9`结束且可被FFmpeg正常解码，Web画面恢复。

CPU对比采用相同进程CPU tick、相同采样方法：

| 状态 | 软件JPEG | MPP MJPEG | 降幅 |
|---|---:|---:|---:|
| 三路推理开启 | 128.7% | 85.4% | 约34% |
| 全部推理关闭 | 40.8% | 15.9% | 约61% |

这里100%代表约一个CPU核。由于MPP样本码流损坏，上表只能保留为问题追踪记录，
不得用于性能结论或发布说明。

## 4. 后续方向

1. 新增H.264编码输出和RTSP/RTP封装，用VLC/ffplay低延迟查看；
2. 若要求浏览器低延迟播放，增加WebRTC网关，或HLS作为高延迟兼容方案；
3. 正确的MPP重试路线是RGA将RGB转换为NV12，再以MPP支持的NV12/DMABUF输入编码；
   必须连续完整解码至少100帧并在浏览器中验证后，才能启用硬件路径；
4. 进行24小时三路MPP+RKNN稳定性和温度测试。
