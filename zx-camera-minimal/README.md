# zx-camera-minimal

RK3588 阶段一最小 V4L2 闭环：MMAP 采集、保存原始帧、FPS/序号统计及
SIGINT/SIGTERM 安全退出。不依赖 Qt、OpenCV、GStreamer 或 Rockchip 专有库。

## 已确认的板端环境

- 节点：`/dev/video22` (`rkisp_mainpath`)
- 驱动：`rkisp_v6`，bus `platform:rkisp0-vir2`
- 类型：`V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE`
- 格式：NV12，2112×1568，1 plane
- stride：2112，sizeimage/bytesused：4,967,424 bytes
- 实测：30 帧连续，约 30.05 FPS
- 编译器：GCC 9.4，支持 C++17
- 当前检查结果：板端尚未找到 `cmake`

## 环境检查

```bash
command -v cmake
command -v c++
test -r /usr/include/linux/videodev2.h
bash scripts/inspect_camera.sh /dev/video22
```

如果 `cmake` 没有输出，请先确认板卡 BSP/SDK 是否自带 CMake，或在主机交叉构建。
不要在未确认板端软件管理策略前直接使用包管理器。

## CMake 编译

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
```

板端暂时没有 CMake 时，可用已存在的 GCC 9.4 做等价的临时验证：

```bash
mkdir -p build
c++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
  -Iinclude \
  src/main.cpp src/camera_config.cpp src/v4l2_camera.cpp \
  -o build/zx-camera_minimal
```

## 运行

```bash
./build/zx-camera_minimal \
  --device /dev/video22 \
  --width 2112 \
  --height 1568 \
  --format NV12 \
  --frames 300 \
  --save-count 3 \
  --output /tmp/zx-camera
```

或使用配置和脚本：

```bash
bash scripts/run_camera.sh
```

## PC 上传并触发板端测试

默认开发板连接为 `zxwy@192.168.10.65`。按当前项目要求，默认密码已经
写入 PC 部署脚本，并通过 `SSH_ASKPASS` 自动提供。在 PC 项目目录执行短测试：

```bash
bash scripts/pc_deploy_and_test.sh
```

完整验收（包含 10 分钟稳定性测试）：

```bash
bash scripts/pc_deploy_and_test.sh --long-test
```

需要临时使用其他 SSH 用户时通过环境变量指定：

```bash
BOARD_USER=root bash scripts/pc_deploy_and_test.sh
```

也可在不修改脚本时覆盖密码：

```bash
BOARD_PASSWORD='新密码' bash scripts/pc_deploy_and_test.sh
```

注意：明文密码会暴露给能够读取该脚本的本机用户。不要把这个脚本提交到公开
仓库；环境稳定后应改用 SSH 公钥认证并删除默认密码。

也可先手工上传项目，然后登录板端执行：

```bash
cd /tmp/zx-deploy/zx-camera-minimal
bash scripts/board_stage1_test.sh
bash scripts/board_stage1_test.sh --long-test
```

PC 脚本会自动把板端最新报告下载到 `board-reports/`。

命令行参数覆盖配置文件。持续运行并用 Ctrl+C 结束：

```bash
bash scripts/run_camera.sh --frames 0 --save-count 0
```

## 文件检查

```bash
find /tmp/zx-camera -maxdepth 1 -type f -name '*.raw' -printf '%f %s bytes\n'
stat -c '%n %s' /tmp/zx-camera/*.raw
```

本次确认的完整 NV12 帧应为：

```text
2112 × 1568 × 3 / 2 = 4,967,424 bytes
```

程序实际按驱动返回的每个 plane `bytesused` 保存；驱动 stride 或格式变化时，
文件大小应以程序日志为准。

## 清理

```bash
rm -r build
rm -r /tmp/zx-camera
```

执行前确认路径无误。删除的编译产物和原始帧不会进入回收站。

## 常见错误

- `Device or resource busy`：用 `fuser /dev/video22` 查看占用者，不要盲目杀进程。
- `Permission denied`：执行 `id` 和 `ls -l /dev/video22`，确认用户属于 `video` 组。
- `poll timeout`：先运行 `scripts/inspect_camera.sh`，再查看最新 `dmesg`。
- `VIDIOC_S_FMT` 失败：核对 `--list-formats-ext`，确保 FOURCC 为四字符。
- 实际尺寸不同：驱动可调整请求值，以启动日志中的 Accepted/Actual 为准。
- 文件偏小：比较程序打印的 `bytes` 与 `stat`；保存失败会明确输出 ERROR。
- Ctrl+C 未即时退出：`poll()` 最多等待 `poll_timeout_ms`，信号通常会中断 poll。

## 阶段一验收清单

- [ ] CMake 构建成功（补齐板端 CMake，或先用 GCC 命令验证）
- [ ] `/dev/video22` 能打开且 capability/格式打印正确
- [ ] 连续采集至少 300 帧
- [ ] 无死锁和无限阻塞
- [ ] Ctrl+C 可安全退出
- [ ] 3 个文件均为驱动报告的有效字节数
- [ ] 序号连续，跳号时统计正确
- [ ] 平均 FPS 接近 30
- [ ] `--frames 0 --save-count 0` 连续运行 10 分钟稳定
- [ ] 运行期间 RSS 无明显持续增长
- [ ] 退出后可立即再次运行
- [ ] 最新 dmesg 无新增 ISP/CSI 错误

通过以上全部项目后，才进入阶段二：最小 Qt 预览。
