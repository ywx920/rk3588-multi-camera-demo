# ZX2603-2 向正点原子 RK3588 SDK 迁移可行性分析

## 1. 结论先行

迁移可行，但不建议“把旧 SDK 中改过的文件整批覆盖到正点原子 SDK”。

推荐策略是：

> 以未修改的正点原子 SDK 为新基线，以 ZX2603-2 原理图和当前已验证硬件
> 结果为依据，逐项重建板级 DTS/配置；每次只启用一个接口，并保留当前
> 可启动镜像作为回退。

难度评估：

- 让新 SDK 启动到串口和 rootfs：中等；
- 恢复大部分已通外设：中等到偏高；
- 恢复 Type-C、显示、PCIe、双摄等复杂拓扑：偏高；
- 解决 OV13850 暗黄画质：高，且不能由换 SDK 自动解决。

迁移有工程价值，因为正点原子 SDK 的板级组织、工具链和用户空间相对完整，
且你的硬件参考了其原理图。但摄像头问题的核心是 ISP30 对应模组的有效 IQ，
不是 Linux 5.10 驱动是否来自正点原子。

## 2. 对比对象

现有已适配 SDK：

```text
/home/wx/1-rk3588-sdk/rk3588_sdk
```

来源/状态：

- LubanCat/Rockchip BSP 基础；
- 已有 ZX2603-2 BoardConfig、U-Boot DTS、Kernel DTS；
- 大部分板级接口已经调通；
- 当前板端系统来自这一分支。

正点原子 SDK：

```text
/home/wx/zdyzrk3588_linux_sdk/01_linux_sdk/atk-rk3588_linux_release_v1.2_20250104
```

来源/状态：

- 正点原子 RK3588 Linux release v1.2；
- 只进行过编译；
- `kernel` 仅有一个未跟踪的 `make.sh`，关键源码没有本地修改；
- 可作为干净迁移基线。

## 3. 已确认的共同点

两边内核 Makefile 均为：

```text
Linux 5.10.160
```

因此设备树 binding、Rockchip media 子系统和多数驱动接口处于同一大代际，
比跨 5.10/6.1 BSP 迁移容易。

两边都含：

```text
kernel/drivers/media/i2c/ov13850.c
```

且都包含：

- 2112×1568 模式；
- 4224×3136 模式；
- 24 MHz xvclk；
- 2 lane；
- 600 Mbps/lane；
- SBGGR10。

这说明传感器基础寄存器表没有明显代际差异。

## 4. 关键差异

### 4.1 板级基线不同

现有 ZX2603-2 DTS：

```text
rk3588-zx2603-2.dts
```

目前继承：

```text
rk3588-lubancat-5.dts
```

正点原子入口：

```text
rk3588-atk-devkit.dts
  -> rk3588-atk-devkit.dtsi
  -> rk3588-atk-cameras.dtsi
  -> rk3588-atk-screen_choose.dtsi
  -> rk3588-atk-lcds.dtsi
  -> rk3588-linux.dtsi
```

二者节点标签、默认启用接口、GPIO 占用、供电模型和摄像头拓扑不同。不能直接把
旧 DTS 覆盖新 DTS，否则容易产生重复 regulator、重复 endpoint、GPIO 冲突或
phandle 不存在。

### 4.2 摄像头接线配置不同

ZX2603-2 当前 J7：

```text
i2c3
xvclk M3
reset GPIO1_A2
pwdn  GPIO1_A3
2 lane split PHY
csi2_dphy1 -> mipi2_csi2 -> rkcif_mipi_lvds2 -> rkisp0_vir2
```

正点原子 RK3588 默认 `ATK_CAMERA_3`：

```text
i2c3
IMX415
xvclk M3
reset GPIO1_B1
power GPIO1_A7
4 lane csi2_dphy0
... -> rkisp0_vir0
```

即使接口连接器外形或部分原理图相似，GPIO、lane 数和 PHY 拓扑也不相同。
迁移时必须以 ZX2603-2 实际原理图和已验证 DTS 为准。

### 4.3 OV13850 驱动差异

现有 SDK 的 OV13850 驱动相对正点原子版本主要增加：

- 传感器 MIPI 寄存器调试打印；
- 电源/GPIO 调试打印；
- 模组和镜头名默认值；
- `RKMODULE_GET_MODULE_INFO_V2 (0x945756c0)` 兼容；
- stream on 后调试延时。

真正影响 sensor 模式的宽高、lane、时钟和主要寄存器表基本一致。因此更换成
正点原子驱动不太可能修复暗黄画面。反而若使用 AIQ v5，需要保留或重新验证
module-info ioctl ABI 兼容。

### 4.4 RKAIQ 与 IQ

正点原子 SDK 的源码头文件声明：

```text
RK_AIQ_VERSION_REAL_V = v5.0x3.0
```

其 Buildroot 配置对 RK3588 使用：

```text
ISP_HW_V30
RKAIQ_TARGET_SOC=rk3588
iqfiles/isp3x/*.json
```

这说明该 SDK 能提供一套内部一致的 RK3588/ISP30/AIQ v5 构建链，值得作为
新基线评估。

但是同名 OV13850 IQ 位于：

```text
external/camera_engine_rkaiq/rkaiq/iqfiles/isp21/
ov13850_ZC-OV13850R2A-V1_Largan-50064B31.json
```

它的 SHA256 是：

```text
eba8f8cbb2b8c4473ccf61d7d899905581cf4d39c132315e8ed4ce98eea358f0
```

内容为：

```text
scene_isp21
```

这恰好就是板端早期崩溃的原始 IQ。正点原子 SDK 的 `isp3x` 目录没有发现
该模组的正式 ISP30 IQ。

因此：

1. 正点原子 SDK 可改善 AIQ 二进制、头文件、内核 ABI 的版本一致性；
2. 它没有现成解决 ZC-OV13850R2A-V1/Largan-50064B31 的 ISP30 标定；
3. 换 SDK 后相机可能“更容易规范启动”，但暗黄画质大概率仍存在；
4. 若构建系统只打包 `isp3x/*.json`，这个 `isp21` 文件甚至不会自动进入
   RK3588 rootfs。

## 5. 现有 SDK 已识别的改动面

内核工作区存在以下已修改文件：

```text
arch/arm64/boot/dts/rockchip/Makefile
arch/arm64/boot/dts/rockchip/overlay/Makefile
arch/arm64/configs/lubancat_linux_rk3588_defconfig
drivers/gpu/drm/rockchip/dw-dp.c
drivers/media/i2c/ov13850.c
drivers/net/ethernet/stmicro/stmmac/stmmac_main.c
drivers/rtc/rtc-hym8563.c
drivers/usb/typec/tcpm/fusb302.c
drivers/usb/typec/tcpm/tcpm.c
```

新增：

```text
rk3588-zx2603-2.dts
ZX2603-2 HDMI overlays
uEnvZX2603-2.txt
```

U-Boot 新增：

```text
arch/arm/dts/rk3588-zx2603-2.dts
configs/zx2603-2-rk3588_defconfig
```

板级/系统新增或修改还包括：

```text
BoardConfig-ZX2603-2-rk3588-ubuntu-xfce.mk
parameter-zx2603-2-ubuntu-fit.txt
hostname/hosts
boot_init.sh
rkaiq_3A.sh
switch bridge/mgmt 配置
IQ files
```

这些改动不能视为一个整体。每一项都要判断是：

- ZX2603-2 硬件必需；
- LubanCat 基线兼容补丁；
- 临时调试代码；
- 可由正点原子原生实现替代。

例如 OV13850 大量寄存器打印属于调试代码，不应直接带入量产基线。

## 6. 建议的迁移方式

### 6.1 不建议

不要：

- 复制整个 `kernel/` 覆盖正点原子内核；
- 复制整个旧 DTS 覆盖 `rk3588-atk-devkit.dts`；
- 复制旧 rootfs 的 `/usr/lib` 和 AIQ 二进制到新 rootfs；
- 混用 v5.0x1.3/v5.0x3.0 的 server、库、头文件；
- 把 ISP21 IQ 改名成 ISP30 IQ；
- 一次启用双摄、DP、Type-C、PCIe 和网络。

### 6.2 推荐

在正点原子 SDK 内新建独立板型，例如：

```text
rk3588-zx2603-2-atk.dts
zx2603-2_atk_defconfig
```

保留正点原子原板配置不变，以便随时验证基线。

迁移资产应形成小补丁集合：

```text
0001-board-minimal-boot.patch
0002-storage-and-rootfs.patch
0003-gmac.patch
0004-usb-and-typec.patch
0005-display.patch
0006-pcie.patch
0007-camera-single-raw.patch
0008-camera-rkisp-rkaiq.patch
```

每个补丁只负责一个闭环。

## 7. 建议实施阶段

### 阶段 M0：冻结和建账

目标：确保当前可用系统可回退。

操作：

1. 备份当前可启动的 loader、uboot、boot、rootfs 和 parameter；
2. 保存板端 `dmesg`、`/proc/cmdline`、分区表、时钟/电源摘要；
3. 保存当前 DTB 和反编译 DTS；
4. 对现有 SDK 每个子仓库导出 `git diff` 和未跟踪文件清单；
5. 建立外设验收表。

验收：任何新镜像失败都能恢复当前系统。

### 阶段 M1：正点原子原始基线

目标：确认未修改 SDK 的编译产物和烧录流程可靠。

先在正点原子开发板配置上完成：

- U-Boot；
- kernel；
- rootfs；
- update image；
- 文件哈希记录。

此阶段不要烧入 ZX2603-2，先确认构建可重复。

### 阶段 M2：ZX2603-2 最小启动

只迁移：

- DDR/loader 选择；
- PMIC/RK806；
- eMMC/SD；
- 串口；
- parameter/分区；
- 一个可用网口或 USB；
- 独立 model/compatible。

暂时禁用：

- 摄像头；
- DSI/复杂显示；
- PCIe FPGA；
- Type-C PD；
- 第二路网口及交换芯片。

验收：

```text
U-Boot稳定
Kernel启动
rootfs挂载
串口登录
重启/关机正常
```

### 阶段 M3：逐个恢复外设

建议顺序：

1. eMMC/SD/USB；
2. GMAC；
3. RTC；
4. HDMI；
5. DP；
6. Type-C/FUSB302；
7. PCIe RC 与 FPGA 枚举；
8. DSI；
9. 摄像头。

每启用一个模块都保存：

- DTS diff；
- 编译日志；
- dmesg；
- 节点状态；
- 功能测试结果。

### 阶段 M4：OV13850 只验证 RAW

先只验证：

```text
I2C probe
chip ID
24 MHz xvclk
reset/pwdn 极性
2112×1568 SBGGR10
2 lane 600 Mbps/lane
连续 RAW 300 帧
无 CSI/D-PHY error
```

不要在这一步引入 RKAIQ 或修改 IQ。

### 阶段 M5：ISP 与 AIQ

固定一个完整版本组合：

```text
kernel camera headers
RKAIQ headers/libraries/server
ISP30 build flags
iq parser
tuning tool/tool server
```

先用正点原子 SDK 中已知可工作的 ISP30 传感器验证 RKAIQ 基线，再接 OV13850。

对 OV13850：

1. 使用正式 ISP30 schema 创建 IQ；
2. 导入可合法复用的 sensor/镜头资料；
3. 先关闭或使用保守值处理未标定模块；
4. 按 AE -> AWB -> BLC/LSC -> CCM -> Gamma/DRC -> NR/Sharp 顺序调试；
5. 最终使用匹配 AIQ v5.0x3.0 的 Tuner 套件。

### 阶段 M6：应用回归

按顺序恢复：

1. `zx-camera-minimal`；
2. UC325 Web 预览；
3. OV13850 Qt 预览；
4. 之后才进入 RGA/RKNN/MPP。

## 8. 工作量估计

在原理图清楚、串口可靠、已有当前工作 DTS 可参考的前提下：

| 工作项 | 估计 | 风险 |
|---|---:|---|
| 迁移框架、差异建账 | 1～2 天 | 低 |
| 最小启动和存储/rootfs | 2～5 天 | 中 |
| 基础 USB/GMAC/RTC/HDMI | 3～7 天 | 中 |
| Type-C/DP/DSI/双网口 | 1～3 周 | 中高 |
| PCIe FPGA 恢复到枚举 | 2～5 天 | 中 |
| 单路 OV13850 RAW/ISP 链路 | 3～7 天 | 中高 |
| 正式 ISP30 IQ 调试 | 2～6 周或更久 | 高 |

这是工程估算，不包含硬件缺陷返板、缺失模组标定数据、Tuner 版本协调时间。

## 9. 是否会改善摄像头问题

分问题回答：

### 可能改善

- AIQ 二进制、头文件、库和构建参数的一致性；
- RK3588 ISP30 的打包路径；
- 正点原子板级脚本和服务管理；
- 使用与其 SDK 对应的 Tuner/tool server 的机会。

### 不会自动改善

- 当前模组的 AE/AWB/CCM/LSC 标定；
- 镜头和 sensor 个体差异；
- ZX2603-2 与正点原子板不同的 reset/pwdn/供电/lane 接线；
- 由错误 IQ 引起的暗黄、压黑。

所以“为了修相机而整体换 SDK”投入产出不确定；“为了建立更干净、接近硬件参考
设计的长期 BSP 基线而迁移”是合理的。

## 10. 最推荐的下一步

不要立刻修改或烧录。下一步只做 **M0 冻结与自动差异建账**：

1. 给两个 SDK 建立只读清单；
2. 导出现有 SDK 各子仓库 patch；
3. 记录所有新增文件；
4. 建立外设验收矩阵；
5. 备份当前板端镜像和 DTB。

完成 M0 并验证可回退后，再进入 M1/M2。摄像头 IQ 调试作为独立支线，不应成为
整套 BSP 迁移是否成功的唯一判断标准。
