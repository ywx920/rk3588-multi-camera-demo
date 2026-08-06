# ZX2603-2当前基线与完整调试过程

## 1. 文档目的

本文把分散在串口日志、摄像头排障记录、Qt/Web阶段报告和RKNN准备文档中的结论
合并为当前可执行基线。详细原始命令和日志仍保留在对应阶段文档，本文不取代原始
证据，而是说明哪些结论仍有效、哪些已经被后续复核推翻。

## 2. 硬件与系统基线

- 板卡：ZX2603-2，RK3588，8 GiB LPDDR4X；
- 内核：Linux 5.10.160；
- 用户空间：Ubuntu 20.04 ARM64及GNOME图形会话；
- 摄像头：J7 OV13850、J8 OV13850、UC325 USB摄像头；
- NPU驱动：RKNPU 0.9.8（20240828），IOMMU模式；
- RKNN Runtime：`librknnrt.so`，目标平台RK3588；
- RGA：`librga.so.2.1.0`及开发头文件；
- 模型：`yolov5s_relu_rk3588_int8.rknn`。

RKNPU日志中的`failed to find power_model`和`failed to get dynamic-coefficient`
表示功耗模型数据不完整，但NPU DRM节点、IOMMU和实际推理已经工作。它不是本阶段
画面故障的直接原因，后续需在功耗/频率调优阶段单独修正。

## 3. 启动与显示调试

最初镜像能够完成DDR、SPL、U-Boot、FIT、内核和设备树加载。串口在
`feb50000.serial: ttyS2 ... is a 16550A`后停止打印，结合启动参数中的
`earlycon`、`ttyFIQ0`及ttyS2切换，不能仅凭这一行判断内核卡死。

U-Boot已启用MIPI0/VP3并按800×1280@60 Hz输出，因此MIPI屏较早点亮。早期日志
缺少HDMI EDID/HPD/PHY初始化，说明当时设备树显示路由主要指向MIPI。后续系统DRM
节点已出现`HDMI-A-1`和`HDMI-A-2`，是否出图仍需结合HPD、EDID和实际接口逐路验证。

早期“只有壁纸、类似iPad且无图标”属于桌面会话/启动器配置问题，不代表内核或
显示驱动停止。当前开发使用GNOME会话，后台Qt脚本会自动发现有效DISPLAY和
XAUTHORITY；只有GDM登录界面时不会强行启动Qt。

## 4. OV13850与媒体链路

最终确认两条独立链路：

```text
J7: ov13850 3-0010 -> csi2-dphy1 -> rkcif-mipi-lvds2 -> rkisp0-vir2
    -> /dev/video-camera0（曾映射video22）

J8: ov13850 2-0010 -> csi2-dphy4 -> rkcif-mipi-lvds4 -> rkisp1-vir1
    -> /dev/video-camera1（曾映射video31）
```

`/dev/videoN`编号会随设备枚举变化，程序和脚本应使用`/dev/video-camera0/1`或
udev/by-path，不应写死video22、video31或UC325的video编号。

OV13850 RAW端为BG10，直接对rkcif RAW节点请求NV12会出现`can't transform`，这是
节点类型使用错误，不是传感器失败。正常预览应使用`rkisp_mainpath`的NV12节点。

`rkaiq_3A_server`会占用RAW节点和sensor subdev，直接测试`/dev/video0`时出现
`Device or resource busy`属于正常所有权冲突。主路径NV12仍可供应用采集。两路
OV13850并发实测均为约30.05 FPS，返回码为0，未出现CSI/ISP overflow。

## 5. IQ与画质恢复

当前SDK最初的OV13850画面异常，通过对照
`zdyzrk3588_linux_sdk/01_linux_sdk/atk-rk3588_linux_release_v1.2_20250104`
中已验证配置，迁移对应OV13850 IQ/AIQ资产和匹配命名后恢复。IQ必须与sensor名称、
模组名称、镜头名称、工作模式、Bayer顺序和RKAIQ版本共同匹配，不能只复制一个XML
或JSON文件就假设生效。

当前J7/J8经过rkisp_mainpath输出NV12并由3A服务控制，画面已经人工确认正常。

## 6. 三套Demo演进

### 6.1 Minimal

Minimal用于隔离V4L2、格式、缓冲和帧率问题，不承担完整桌面交互。它是判断
“摄像头/驱动有问题”还是“Qt/Web状态机有问题”的最小证据链，不能删除。

### 6.2 Qt

Qt从单路采集扩展到J7、J8、UC325三路，历经以下修复：

1. 采集移出UI线程，Stop只暂停处理并继续DQBUF/QBUF；
2. OV13850使用RGA缩放和NV12转RGB，支持V4L2 EXPBUF/DMABUF；
3. 推理改为集中Scheduler、1至3个worker、每路只保留最新帧；
4. 修复反复Start/Stop后`waiting for frame`；
5. 固定卡片尺寸和2×2布局，解决第三路被挤出屏幕；
6. Stop时清理迟到信号和旧Pixmap，解决随机残影；
7. 增加逐路摄像头/推理开关、全局推理开关；
8. 增加IoU跟踪、框平滑、结果超时和空场景自适应降频；
9. 增加后台启动、统一停止和稳定性监控。

### 6.3 Web

Web从UC325单路MJPEG扩展为JSON动态三路，复用Qt的RGA、RKNN和YOLO后处理。
新增逐路及全局摄像头/推理控制、响应式监控卡片、状态接口和后台脚本。

MPP RGB888直编码曾因只检查SOI/JFIF头而被误判成功。完整帧复核发现DQT/SOS等
错误，浏览器无法解码。当前已增加首帧完整解码和尺寸校验，失败后使用
`Qt JPEG fallback`。此前基于损坏MPP码流得到的低CPU数据全部作废。

Web第十阶段增加按需处理：无人订阅时继续回收V4L2缓冲，但跳过转换、推理、叠框
和JPEG，30秒后释放RKNN上下文。默认工作宽800、JPEG质量72；J7/J8发布约7.51
FPS，UC325约10.00 FPS。无人观看的初始状态约2% CPU、16 MB RSS；三路同时订阅
的现场样本约83% CPU，推理单次约20至23 ms。

## 7. 当前运行边界

- Qt与Web会独占同一摄像头节点，不能并行运行；
- Cheese/ffplay对RKISP multiplanar节点支持不一致，不能作为唯一验收工具；
- SSH X11转发不是板端本地图形会话，Qt应使用板端GNOME DISPLAY；
- Web当前仍为软件JPEG，三路可用但不适合直接扩展到8路；
- `/dev/rknpu*`不存在并不表示NPU不可用，当前NPU通过DRM render节点工作；
- MPP后续必须用完整100帧解码及浏览器实测作为准入条件。

## 8. 当前有效启动入口

```bash
# Web
cd /home/zxwy/zx-camera-web-demo
bash scripts/start_background.sh

# Qt（需已登录GNOME；启动前先停Web）
cd /home/zxwy/zx-camera-qt-demo
bash scripts/start_background.sh

# 统一停止
/home/zxwy/stop_camera_demos.sh
```

Web地址：`http://192.168.10.65:8080/`。

2026-08-06本次文档整理结束时，Web板端状态为：

| 通道 | viewers | 发布FPS | NPU单次 | 编码后端 | 错误 |
|---|---:|---:|---:|---|---|
| J7 | 1 | 7.51 | 约22.1 ms | Qt JPEG fallback | 无 |
| J8 | 1 | 7.51 | 约22.0 ms | Qt JPEG fallback | 无 |
| UC325 | 1 | 10.00 | 约21.2 ms | Qt JPEG fallback | 无 |

三路均为`camera=true`、`detection=true`。该表是现场瞬时样本，用于确认交付状态，
不是长时间性能上限结论。

## 9. 文档证据映射

- 摄像头和IQ：`01`、`06`；
- SDK迁移：`02`、`03`；
- 部署：`04`；
- RKNN和版本：`05`、`07`；
- 多路优化：`multi-camera-stage3`至`stage8`；
- MPP错误复核：`stage9`；
- Web黑屏修复和全局开关：`web-camera-recovery...`；
- Web按需处理：`web-stage10...`；
- Qt完整优化细节：`zx-camera-qt-demo/docs/optimization-log-2026-08-05.md`。

## 10. GitHub备份状态

2026-08-06在`/home/wx/2-demoRK`初始化Git，保留源码、配置、脚本、IQ、ONNX、RKNN
模型、测试报告和调试文档；通过`.gitignore`排除可重建虚拟环境、下载缓存、编译
目录、1.8 GB RKISP Tuner资料和大型RAR。首次提交后补充项目总README。

HTTPS推送先后遇到远程已有初始提交和Token认证失败。改用SSH后，GitHub官方
Ed25519指纹验证正确，但当前网络把`github.com`解析到代理地址，IPv6连接在握手后
卡住。使用`ssh -4`后成功认证为`ywx920`，并将仓库级`core.sshCommand`固定为IPv4。

远程`main`仅有GitHub自动生成的README提交`590b085`。本地执行允许无共同历史的
合并，README发生add/add冲突后保留本工程完整版本，生成合并提交`94f7606`并成功
推送。当前远程为：

```text
git@github.com:ywx920/rk3588-demo-camera.git
```

日常备份只需`git add -A`、`git commit`和`git push`，不再使用Token。
