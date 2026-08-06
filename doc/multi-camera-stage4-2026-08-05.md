# RK3588多路视觉第四阶段交付记录

## 本阶段目标

- Web端实现J7、J8、UC325三路YOLOv5实时检测；
- Web与Qt两套程序均提供逐路检测功能开关；
- 关闭检测时继续预览，避免反复操作摄像头流导致RKISP等待帧；
- Web界面同步显示输出FPS、NPU耗时和目标数量。

## 实现结果

Web复用了Qt Demo中的`RknnYolov5Engine`、YOLO后处理和`Nv12Converter`。
OV13850使用RGA将2112×1568 NV12缩放、转RGB，UC325解码MJPEG；检测结果由
QPainter绘制后编码成浏览器可直接查看的MJPEG流。RKNN上下文采用按需创建，
模型初始化通过互斥锁串行执行，规避多路首次启动时底层全局资源并发初始化。

Web新增接口：

```text
GET  /status
POST /toggle?id=j7&enabled=0
POST /toggle?id=j7&enabled=1
```

Qt每张摄像头卡片新增`Detection`复选框。关闭时仅禁止该通道向集中推理调度器
提交任务；采集线程和预览保持运行。停止通道时同步禁用调度，重新启动时按当前
复选框状态恢复。

Web每张卡片另增加摄像头软开关。关闭后停止发布新画面并停止该路推理，但采集
线程继续DQBUF/QBUF；重新打开不执行STREAMOFF/STREAMON，因此不会破坏双路
OV13850流水线。状态接口新增`camera`字段，关闭时推理指标清零。

两套程序分别增加后台启动/停止脚本，并在板端部署统一停止入口：

```text
/home/zxwy/zx-camera-web-demo/scripts/start_background.sh
/home/zxwy/zx-camera-qt-demo/scripts/start_background.sh
/home/zxwy/stop_camera_demos.sh
```

Qt后台脚本会从GNOME Shell进程自动发现`:1`及GDM Xauthority，不依赖SSH自身
的DISPLAY。两套程序会竞争V4L2节点，因此启动脚本会阻止Web与Qt同时运行。

## 板端实测

板端原生ARM64编译Web和Qt均成功。Web三路同时检测的状态样例：

- J7：约9.8 FPS输出，NPU约23.7 ms；
- J8：约9.8 FPS输出，NPU约22.6 ms；
- UC325：约15.1 FPS输出，NPU约20.7 ms。

J7关闭检测后`detection=false`，NPU耗时、推理FPS和目标数均归零，视频输出继续；
重新打开后约2秒内恢复检测。三路状态均无采集错误。

新增摄像头开关实测：J7关闭后发布帧计数停止、`camera=false`且推理指标归零，
J8/UC325持续运行；重新打开J7后画面和检测均恢复。Qt和Web后台进程均通过PID
文件启动并由统一脚本完成安全退出。

## 运行方式

```bash
cd /home/zxwy/zx-camera-web-demo
./zx-camera_web --config config/cameras.json --port 8080 \
  --model /home/zxwy/j7-yolov5/models/yolov5s_relu_rk3588_int8.rknn
```

PC浏览器访问`http://192.168.10.65:8080/`。Qt在板端桌面终端中运行现有
`scripts/run_board_all.sh`，使用各卡片的`Detection`开关控制对应通道。
