# ZX2603-2 开发与移植文档索引

更新时间：2026-08-06

本目录集中保存 ZX2603-2 上的摄像头最小闭环、Qt/Web 预览、RKAIQ/IQ
排障记录，以及从现有 LubanCat BSP 向正点原子 RK3588 SDK 迁移的分析。

## 文档

1. [01-camera-debug-and-development-record.md](01-camera-debug-and-development-record.md)
   - OV13850 V4L2 最小采集
   - RKAIQ/IQ 兼容测试
   - Qt 阶段二接口
   - UC325 与 Web MJPEG 预览
   - 当前已知问题和可复现实验

2. [02-sdk-migration-feasibility-analysis.md](02-sdk-migration-feasibility-analysis.md)
   - 两套 SDK 的只读对比结果
   - 是否值得迁移
   - 对 OV13850 画质的真实影响
   - 风险、工作量和分阶段迁移方案

3. [03-atk-sdk-migration-implementation-record.md](03-atk-sdk-migration-implementation-record.md)
   - 已迁移的板级、驱动、用户空间和调试资产
   - PC 构建结果
   - 上板前的重要边界与后续验证入口

4. [04-three-camera-demo-build-and-deploy.md](04-three-camera-demo-build-and-deploy.md)
   - J7、J8、UC325 三路 Demo 的板端依赖、构建与运行方式

5. [05-rknn-realtime-detection-next-steps.md](05-rknn-realtime-detection-next-steps.md)
   - 当前 RKNPU/RKNN/RGA 环境判断
   - RKNN 冒烟测试、模型转换和三路实时识别实施顺序
   - 性能、稳定性及 NPU 功耗模型验收建议

6. [06-2026-08-04-debug-record.md](06-2026-08-04-debug-record.md)
   - J7/J8 双路链路、RKAIQ 占用和并发验证
   - minimal、Web、Qt 三路扩展过程
   - Qt Start/Stop 状态机修复与 NPU 环境初查

7. [07-j7-yolov5-rknn-preparation-and-version-guide.md](07-j7-yolov5-rknn-preparation-and-version-guide.md)
   - J7 单路 YOLOv5 RKNN 实时推理的准备与实施步骤
   - Toolkit2、Runtime、RKNPU、RGA 和后处理的版本匹配原则
   - 静态帧、实时流、性能稳定性及故障回退检查表

8. [08-current-baseline-and-complete-debug-timeline-2026-08-06.md](08-current-baseline-and-complete-debug-timeline-2026-08-06.md)
   - 从启动、双OV13850、IQ、三套Demo到RKNN检测的完整调试时间线
   - 当前有效软硬件基线、设备映射、实测结果和已废弃结论
   - Qt/Web当前交付状态及仍存在的性能边界

9. [09-qt-web-next-development-roadmap-2026-08-06.md](09-qt-web-next-development-roadmap-2026-08-06.md)
   - Qt与Web下一阶段的独立开发建议
   - 每阶段实现内容、验收指标、风险和推荐顺序
   - 8路扩展、统一推理调度、硬件编解码和监控方案

10. 多路优化阶段记录
    - `multi-camera-stage3`至`multi-camera-stage8`：动态配置、集中调度、固定布局、
      全局控制、跟踪和负载自适应
    - `multi-camera-stage9`：MPP JPEG实验及完整码流复核勘误
    - `web-camera-recovery-and-global-camera-control`：Web黑屏恢复
    - `web-stage10-demand-and-bandwidth-optimization`：按需处理与滚动FPS

11. [10-github-backup-and-ssh-debug-record-2026-08-06.md](10-github-backup-and-ssh-debug-record-2026-08-06.md)
    - 5.1 GB工作目录的Git备份范围与忽略规则
    - HTTPS Token认证失败、SSH IPv6卡住及强制IPv4修复
    - 远程初始README冲突处理和最终推送结果

## 关联历史文档

早期 OV13850 详细日志仍保留在：

```text
/home/wx/2-demoRK/zx-camera-minimal/docs/2026-07-27-rk3588-ov13850-debug-record.md
```

本目录文档是面向后续开发的汇总，不删除原始报告、测试压缩包或源码。
