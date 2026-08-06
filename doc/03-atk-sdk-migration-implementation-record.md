# ZX2603-2 迁移到正点原子 SDK 实施记录

日期：2026-07-30

## 路径

- 已适配源 SDK：`/home/wx/1-rk3588-sdk/rk3588_sdk`
- 迁移目标 SDK：`/home/wx/zdyzrk3588_linux_sdk/01_linux_sdk/atk-rk3588_linux_release_v1.2_20250104`
- 完整迁移文档：目标 SDK 的 `docs/zx2603-2-porting/`

## 已实施内容

1. 新建独立 `zx2603_2_rk3588_defconfig`，不覆盖 ATK 官方板型。
2. 迁移 ZX2603-2 内核 DTS、defconfig 和相关 binding。
3. 迁移 OV13850、Type-C/TCPM/FUSB302、DP、stmmac、HYM8563 驱动修改。
4. 迁移 ZX2603-2 U-Boot DTS 和 defconfig。
5. 迁移 JL6107SC 源码、管理程序、库和配置。
6. 迁移 ISP30 OV13850 兼容 IQ、摄像头规则和音频 UCM2 配置。
7. 迁移 PCIe DMA 试验源码、工具源码、文档和 FPGA RTL 模板，但不默认加载。
8. 归档旧调试记录、Ubuntu 分区参数、extboot 和 HDMI overlay。
9. 添加板端信息采集脚本 `zx2603-board-check.sh`。

## PC 构建结果

- Kernel：通过。生成新 `Image`、`rk3588-zx2603-2.dtb`、`resource.img` 和
  `boot.img`。
- U-Boot：通过。生成 `uboot.img` 和 RK3588 SPL loader。
- Rootfs：通过。已生成 CPIO、EXT2/EXT4、SquashFS，并确认 ZX2603-2 专用
  overlay 实际进入 target；最终修正构建日志为
  `docs/zx2603-2-porting/build-logs/rootfs-build-overlay-fix.log`。
- Rootfs 内容检查：板端检查脚本、JL6107SC CLI/Web/动态库、摄像头与网口
  udev 规则、ALSA UCM2 和 OV13850 ISP30 IQ 均已进入 target。
- PCIe DMA 试验驱动和用户态工具：在目标 SDK 的新内核与工具链上编译通过，
  但未设为自动加载。
- Recovery：通过，FIT 已包含 ZX2603-2 DTB、当前内核和 recovery ramdisk。
- 统一升级包：通过，生成 `rockdev/update.img`。首次打包暴露 ATK 通用参数只为
  U-Boot 预留 4 MiB；现已新增 ZX2603-2 专用参数，将 U-Boot 扩至 8 MiB 并
  顺延后续分区。最终包内地址已逐项核对。

内核第一次构建发现 ATK 新版摄像头头文件与旧 OV13850 V2 ioctl 兼容补丁值相同，
导致重复 case；删除多余兼容 case 后构建通过。

## 重要边界

- 当前属于“可构建的第一版移植候选”，不是实板所有接口已经验收。
- OV13850 ISP30 IQ 只解决 AIQ 兼容启动基线，不代表 AE/AWB/LSC/CCM 已完成标定。
- ATK Buildroot 继续使用其自身 `parameter.txt`，没有启用旧 Ubuntu 分区参数。
- HDMI overlay 仅归档；基础 DTS 已包含显示配置。
- PCIe DMA 暂不启用，须先确认 FPGA BAR0 不再返回 `0xFFFFFFFF`。

## 下一步

按目标 SDK 中 `docs/zx2603-2-porting/BUILD_FLASH_TEST_GUIDE.md` 构建和烧录，
然后严格按 `VALIDATION_CHECKLIST.md` 从最小启动闭环开始逐项验证。每次只处理一个
接口，并保存串口、dmesg 和测试命令输出。

更完整的 PC 构建状态见目标 SDK：

```text
docs/zx2603-2-porting/BUILD_RESULT.md
```

最终升级包：

```text
/home/wx/zdyzrk3588_linux_sdk/01_linux_sdk/atk-rk3588_linux_release_v1.2_20250104/rockdev/update.img
SHA256: c2bda1281658002a5cf2b26024639b92591e8ac58de3b26ea64e983ec493ea25
```
