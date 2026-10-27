# ZX Camera Web Demo

动态多路 Web 预览与YOLOv5实时检测：J7/J8 OV13850经RGA完成NV12缩放和
RGB转换，UC325解码MJPEG后送入RKNN。通道由`config/cameras.json`生成，默认地址分别为
`/j7.mjpg`、`/j8.mjpg`、`/uc325.mjpg`。

`OV13850/UC325 -> V4L2 -> RGA/Qt -> RKNN YOLOv5 -> 检测框 -> HTTP MJPEG`

构建依赖C++17、Qt5 Core/Gui、libjpeg、librga和librknnrt，不依赖OpenCV或
GStreamer。RKNN/RGA实现与Qt Demo复用，避免两套程序出现不同的预处理结果。

板端检测到`rockchip_mpp`开发库时会编译实验性MPP MJPEG路径，但当前板端MPP 1.5.0
直接编码RGB888所得码流不能通过完整JPEG解码。程序会对首帧进行完整解码和尺寸校验，
失败后永久切换到Qt JPEG，避免浏览器黑屏。Web状态栏显示实际后端：
`MPP MJPEG`表示已通过校验的硬件编码，`MJPEG passthrough`表示UC325原始码流直通，
`Qt JPEG fallback`表示安全的软件回退。

新版页面使用响应式监控卡片，在PC、平板和手机宽度下自动重排，并通过
`/status`显示各路分辨率、输出FPS、NPU耗时、目标数量和错误状态。每张卡片的
“检测开启/关闭”按钮可独立控制该路推理；“摄像头开启/关闭”按钮用于暂停或恢复
该路画面发布。摄像头软关闭后底层仍回收V4L2缓冲，避免再次开启时出现
`waiting for frame`。跳过的OV13850帧只做
DQBUF/QBUF，不再复制整张NV12图像。UC325节点通过sysfs名称和index自动查找，
不依赖易变化的`/dev/videoN`编号。

页面顶部提供“开启全部摄像头”“关闭全部摄像头”“开启全部推理”和“一键关闭推理”。
检测标签中的`#ID`来自轻量IoU目标跟踪，用于改善低推理帧率下的框位置跳动。

空场景连续无目标时，推理间隔会逐级扩大到2/3/4倍（最长1500 ms），重新检测
到目标后立即恢复配置频率；摄像头采集和Web发布不会因此停止。

板端编译：

```bash
cd /home/zxwy/zx-camera-web-demo
bash scripts/build_board.sh
```

板端运行：

```bash
bash scripts/run_uc325_web.sh
```

指定配置：

```bash
./zx-camera_web --config config/cameras.json --port 8080 \
  --model /home/zxwy/j7-yolov5/models/yolov5s_relu_rk3588_int8.rknn
```

后台运行与停止：

```bash
bash scripts/start_background.sh
bash scripts/stop_background.sh
```

默认只监听开发板的 `127.0.0.1`，服务没有内置身份认证。
PC 远程预览建议通过 SSH 隧道，在 PC 终端执行（替换用户和地址）：

```bash
ssh -N -L 8080:127.0.0.1:8080 <BOARD_USER>@<BOARD_IP>
```

保持隧道连接，在 PC 浏览器打开：

```text
http://127.0.0.1:8080/
```

配置中的`inference_fps`限制每路检测频率，`work_width`限制预览/推理工作图
宽度，`encode_every`控制HTTP输出抽帧，`jpeg_quality`控制软件JPEG质量。当前稳定
默认值为5 FPS推理、800宽工作图、质量72；OV13850约7.5 FPS发布，UC325约10 FPS。
没有浏览器订阅某一路时，该路仍持续回收V4L2缓冲，但跳过图像转换、推理和编码，
避免后台空耗CPU；空闲30秒后释放该路RKNN上下文以回收内存。恢复订阅时会重新加载
模型，因此首帧可能略慢。`/status`中的`viewers`可用于确认实际订阅数。

也可通过HTTP接口控制，例如关闭和重新开启J7检测：

```bash
curl -H 'Content-Type: application/json' -X POST 'http://127.0.0.1:8080/toggle?id=j7&enabled=0'
curl -H 'Content-Type: application/json' -X POST 'http://127.0.0.1:8080/toggle?id=j7&enabled=1'
```

全部推理控制接口：

```bash
curl -H 'Content-Type: application/json' -X POST 'http://127.0.0.1:8080/inference-all?enabled=0'
curl -H 'Content-Type: application/json' -X POST 'http://127.0.0.1:8080/inference-all?enabled=1'
```

摄像头软开关接口：

```bash
curl -H 'Content-Type: application/json' -X POST 'http://127.0.0.1:8080/camera-toggle?id=j7&enabled=0'
curl -H 'Content-Type: application/json' -X POST 'http://127.0.0.1:8080/camera-toggle?id=j7&enabled=1'
```

全部摄像头软开关接口：

```bash
curl -H 'Content-Type: application/json' -X POST 'http://127.0.0.1:8080/camera-all?enabled=0'
curl -H 'Content-Type: application/json' -X POST 'http://127.0.0.1:8080/camera-all?enabled=1'
```

后台启动及统一关闭：

```bash
cd /home/zxwy/zx-camera-web-demo
bash scripts/start_background.sh

# 可在任意目录执行，同时兼容关闭Web或Qt Demo
/home/zxwy/stop_camera_demos.sh
```

控制接口仅接受带 `Content-Type: application/json` 的 POST 请求；GET 不再修改状态。
接收请求头设有超时和大小限制，HTTP 客户端并发上限为 32，完成的连接任务会及时回收。
状态接口会转义 JSON 字符串，配置中的显示文字会进行 HTML 转义。

确需局域网直连时，在受控网络中运行：

```bash
WEB_BIND_ADDRESS=0.0.0.0 bash scripts/run_uc325_web.sh
# 直接运行可使用 --bind <受控网卡IPv4地址>
```

显式开放监听不会增加身份认证，请配合防火墙或带认证的反向代理，不要直接公开摄像头画面和控制接口。
本次安全检查未在实际 RK3588 板卡上执行；部署前仍需重新编译并验证。

停止服务使用 `Ctrl+C`。

不依赖 Qt 或摄像头的 HTTP 安全逻辑测试（在 Web 工程目录执行）：

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Iinclude tests/http_security_test.cpp -o /tmp/zx-http-security-test
/tmp/zx-http-security-test
```
