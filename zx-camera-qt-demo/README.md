# zx-camera-qt-demo

阶段二：J7/J8 OV13850 与 UC325 三路并发 Qt5 预览。

当前包含：

- 后台线程V4L2 MMAP采集；
- OV13850：NV12输入和独立CPU转换模块；
- UC325：MJPEG输入和Qt JPEG插件解码；
- 合成NV12和单帧NV12回归测试路径；
- Start、Stop、FPS、状态和安全退出；
- `shared_ptr<const QImage>`跨线程传递。
- J7、J8和UC325均支持YOLOv5 RKNN实时推理、检测框和NPU耗时显示；
- 每路卡片提供独立的推理按钮，关闭后保留实时预览但不再提交NPU任务；
- 底部提供“开启全部推理”和“一键关闭推理”，全局关闭会停止调度并清空检测框；
- 每路使用明确的“开启/关闭摄像头”和“开启/关闭推理”立体按钮；
- 根据当前主显示器可用分辨率自动选择1、2或3列，低分辨率下使用纵向滚动，
  画面开启后也不会把窗口撑出屏幕；
- 卡片和画面框在窗口创建时固定尺寸，实时Pixmap及NPU长状态文本不会重新挤压
  网格；检测框使用轻量IoU时序平滑以降低跳动；
- 界面固定为2×2：J7/J8位于顶部，UC325位于左下，右下预留第四路；关闭后
  丢弃事件队列中的迟到帧，避免旧画面随机回写；
- 连续空场景会将推理提交间隔逐级扩大到2/3/4倍，检测到目标后恢复正常频率；
- 推理结果超时会自动清除旧框，避免异常时残留“幽灵检测框”；
- 检测标签带稳定的`#ID`，使用轻量IoU匹配和平滑降低低推理帧率下的框跳动；
- 推理线程只保留最新帧，不累积延迟。
- 当前版本由集中`InferenceScheduler`管理1～3个RKNN worker/context，而不是每路
  永久绑定一个上下文；各通道可单独推理，也可同时推理。旧版“每路独立上下文”
  仅是早期实现，已被最新帧覆盖与round-robin公平调度替代。

三路各使用独立 V4L2 采集线程，可分别启停或 Start All/Stop All。

性能优化默认值：摄像头保持30 FPS采集，界面最多15 FPS，推理最多8 FPS，
OV13850由RGA直接从2112×1568 NV12缩放转换为960宽RGB工作图。不参与处理的
采集帧只执行DQBUF/QBUF，不复制整帧数据。

多路推理由集中调度器管理，默认只创建3个RKNN worker/context。各通道只保留
最新一帧并采用round-robin公平调度；界面中的`actual FPS`是该通道真正完成的
推理帧率，`drop`是等待NPU期间被新帧覆盖的旧帧数。

摄像头列表来自`config/cameras.json`，可增加、删除或通过`enabled`开关通道，
不需要再修改`main.cpp`。OV13850驱动支持EXPBUF时，程序会直接使用
`V4L2 DMABUF -> RGA`；不支持时自动回退到MMAP虚拟地址路径。

## PC编译与回归

```bash
cd /home/wx/2-demoRK/zx-camera-qt-demo
bash scripts/build_pc.sh
bash scripts/run_pc.sh --source synthetic --width 640 --height 480 --fps 30
```

单帧NV12：

```bash
bash scripts/run_pc.sh --source nv12 \
  --input /tmp/zx-camera-sample-2112x1568-NV12.raw \
  --width 2112 --height 1568 --fps 30
```

## 板端部署

PC执行：

```bash
bash scripts/pc_deploy_board.sh
```

板端检查：

```bash
cd /home/zxwy/zx-camera-qt-demo
bash scripts/check_board_env.sh
```

板端当前已确认Qt5运行库以及JPEG/XCB插件存在，但缺少CMake、qmake和
Qt5开发元数据。补齐开发环境后执行：

```bash
bash scripts/build_board.sh
```

必须从板端桌面终端启动GUI，SSH默认没有`DISPLAY`。

UC325：

```bash
bash scripts/run_board_uc325.sh
```

OV13850：

```bash
bash scripts/run_board_ov13850.sh
```

J8 OV13850：

```bash
bash scripts/run_board_j8.sh
```

三路同时显示和推理：

```bash
bash scripts/run_board_all.sh
```

从SSH或终端后台启动（脚本自动发现当前GNOME会话的DISPLAY和XAUTHORITY）：

```bash
cd /home/zxwy/zx-camera-qt-demo
bash scripts/start_background.sh
```

后台脚本默认自动开启三路摄像头。需要只打开界面、由用户手动开启时：

```bash
AUTO_START=0 bash scripts/start_background.sh
```

必须先在板端登录GNOME图形桌面；如果当前只有GDM登录界面，脚本会明确提示，
不会再用错误的`:0`启动后崩溃。

关闭可执行本项目脚本，或者使用板端统一停止脚本：

```bash
bash scripts/stop_background.sh
/home/zxwy/stop_camera_demos.sh
```

Qt和Web会独占相同摄像头节点，不能同时运行；后台启动脚本会检测冲突并提示先
执行统一停止脚本。

24小时资源与错误监控：

```bash
cd /home/zxwy/zx-camera-qt-demo
bash scripts/start_stability_monitor.sh

# 需要提前结束时
bash scripts/stop_stability_monitor.sh
```

使用其他配置文件：

```bash
./build-board/zx-camera_qt_demo --source all \
  --config /path/to/cameras.json --inference
```

可通过环境变量调整负载，无需重新编译：

```bash
# 默认平衡模式
DISPLAY_FPS=15 INFERENCE_FPS=8 WORK_WIDTH=960 \
  NPU_WORKERS=3 \
  bash scripts/run_board_all.sh

# 八路扩展时建议从这个低负载参数开始
DISPLAY_FPS=10 INFERENCE_FPS=5 WORK_WIDTH=640 \
  NPU_WORKERS=3 \
  bash scripts/run_board_all.sh

# 三路画质优先模式
DISPLAY_FPS=20 INFERENCE_FPS=10 WORK_WIDTH=1280 \
  bash scripts/run_board_all.sh
```

`WORK_WIDTH`只影响预览和推理前的工作图，不改变摄像头及RKISP的原始采集
分辨率。推理模型仍使用其自身固定输入尺寸。

程序窗口出现后可分别点击每路的 `Start`，或点击 `Start All`。`Stop` 只暂停该路
显示和推理，并保持底层采集队列流动，避免OV13850反复STREAMOFF后进入
`waiting for frame`。

需要临时降低负载时，直接点击对应卡片的“关闭推理”按钮即可；该操作不会关闭
摄像头，也不会触发STREAMOFF。再次打开后从最新一帧恢复检测，不会补算旧帧。

上述脚本默认启用YOLOv5。模型位置为：

```text
/home/zxwy/j7-yolov5/models/yolov5s_relu_rk3588_int8.rknn
```

等价的完整命令：

```bash
cd /home/zxwy/j7-yolov5
/home/zxwy/zx-camera-qt-demo/build-board/zx-camera_qt_demo \
  --source ov13850 --device /dev/video-camera0 \
  --width 2112 --height 1568 --format NV12 --fps 30 \
  --inference --auto-start \
  --model /home/zxwy/j7-yolov5/models/yolov5s_relu_rk3588_int8.rknn
```

也可直接覆盖设备、格式和尺寸：

```bash
./build-board/zx-camera_qt_demo \
  --source uc325 --device /dev/video40 \
  --width 1280 --height 720 --format MJPG --fps 30 \
  --inference --auto-start \
  --model /home/zxwy/j7-yolov5/models/yolov5s_relu_rk3588_int8.rknn
```

三路同时推理会共享RK3588 NPU资源，单路的推理帧率可能低于只开一路时；采集与
界面线程不会等待推理完成，因此不会不断积压旧帧。

板端三路实机无界面压测（15 FPS显示上限、8 FPS推理上限、960工作宽度）中，
Qt进程约占一个CPU核心，整机仍有约81%～84%空闲CPU。实际桌面合成会额外增加
一些GPU/CPU开销，应以桌面终端运行时的`top`结果为最终依据。

## 验收

- 启动窗口时不自动打开摄像头；
- Start后后台线程打开所选节点并显示画面；
- UI线程不执行V4L2采集；
- FPS和累计帧数持续更新；
- Stop只暂停显示和推理，采集线程继续DQBUF/QBUF以保持RKISP链路；
- 关闭窗口时才STREAMOFF、释放mmap/fd并销毁RKNN context；
- 重复Start/Stop不崩溃；
- 关闭窗口后工作线程安全退出；
- UC325连续预览10分钟无崩溃或明显内存增长；
- Stop后最小采集程序可以立即重新打开设备。
