# J7 YOLOv5 RKNN PC 工作区

用于在 x86_64 Linux PC 上完成 YOLOv5 ONNX 导出、RKNN 转换、模拟器验证和版本留档。

本工作区以当前 RK3588 BSP 文档声明的 RKNN-Toolkit2 1.5.2 为兼容基线。系统 Python
保持不变，转换工具安装在隔离环境中。

详细环境结果和后续命令见 `docs/PC_ENVIRONMENT_RESULT.md`。

快速验证：

```bash
cd /home/wx/2-demoRK/j7-yolov5-rknn-pc
source scripts/activate.sh
scripts/check_environment.sh
scripts/run_official_yolov5.sh
```

已经生成并验证的 RK3588 模型位于：

```text
models/generated/yolov5s_relu_rk3588_int8.rknn
```

