# Waveshare 官方摄像头 demo 对照

本目录用于在同一块 RP2350-Touch-LCD-2-C 和同一个 OV5640 上对照官方预览效果。

## 来源和文件

- [官方板卡 Wiki](https://www.waveshare.com/wiki/RP2350-Touch-LCD-2)
- [官方示例压缩包](https://files.waveshare.com/wiki/RP2350-Touch-LCD-2/RP2350-Touch-LCD-2.zip)
- 摄像头示例源码位于压缩包的 `RP2350-Touch-LCD-2/C/02-CAM/`。
- 预编译固件位于压缩包的 `RP2350-Touch-LCD-2/C/02-CAM/uf2/02-CAM.uf2`，在 `C/uf2/` 下也有一份。

已经从此前下载的官方资料包中原样提取 `02-CAM.uf2` 到本目录，无需重新编译。该文件受根目录 `*.uf2` 规则忽略，不进入 Git；重新克隆仓库时，需要从上述官方压缩包取得。

提取文件大小为 **89600 字节**，SHA-256 为：

```text
3a3e065d316280eec9ed3eebbce74ed1778a1c0eb7a563e6d04cad9afbec4b65
```

源压缩包 SHA-256 和第三方许可见 `../CamWallet/THIRD_PARTY_NOTICES.txt`。官方固件包含一个 Absolute family UF2 块及 RP2350 Arm Secure family 程序块，保留原始内容。

## 手动对照步骤

1. 保留自己工程已生成的 `../CamWallet/build/CamWallet.uf2`，供对照后恢复。
2. 保持摄像头连接。按住 BOOT，按下并松开 RESET，再松开 BOOT，使电脑出现 RP2350 盘符。
3. 将本目录的 **`02-CAM.uf2`** 复制到 RP2350 盘，等待自动重启。
4. 移动物体或镜头，比较官方预览与 CamWallet 的更新速度和延迟，保持同样的光照与距离。
5. 对照完成后，再进入 BOOTSEL 模式，将自己的 `CamWallet.uf2` 复制到 RP2350 盘，即可恢复当前工程。

这一步烧录的是厂商的独立程序，不使用 CamWallet 的错误提示、日志格式和 LCD 方向修正。若需要修改官方示例，应从上述资料包单独打开 `C/02-CAM/`，保留 CamWallet 工程作为现有工作。

## 已确认的实现差异

- 官方 `lib/CAM/cam.c` 使用 PIO 接收像素、16 位 DMA 搬运，DMA 中断中立即重新开始向采集缓冲写入。
- 官方 `examples/LCD_2in_test.c` 在主循环中将采集画面复制到另一个显示缓冲，然后刷新屏幕。采集仍在后台进行；这不是通过所有权交换实现的双缓冲，复制过程可能与采集并发。
- 官方 `lib/Config/DEV_Config.c` 将系统/外设时钟设为 150 MHz，并向 `spi_init()` 请求 150 MHz。按 SPI 至少二分频计算，实际最高为 75 MHz，不能将请求值写成实际总线速度。
- 官方 `lib/LCD/LCD_2in.c` 最终调用 `spi_write_blocking()` 发送画面，LCD 刷屏没有使用 SPI DMA。
- 旧版 CamWallet 采用单帧采集、刷屏后重新等待帧同步；LCD 请求 40 MHz，在 150 MHz 外设时钟下分频为 37.5 MHz。
- 当前 CamWallet 已改为两块独立缓冲交替使用，在刷新上一帧前启动下一帧采集。PIO 每帧重新对齐 VSYNC，32 位 DMA 保持像素字节顺序，主循环不做逐像素复制；采集不会覆盖正在显示的缓冲。
- 当前 LCD 请求 75 MHz，启动日志输出实际 SPI 速度，屏幕方向仍使用已验证的 `0x48`。没有调整系统时钟或传感器 PLL/行帧时序。

官方示例提供了可对照的实现，没有在上述资料中给出此配置可保证的 FPS。

## 当前实测与待确认项

2026-10-29，开发者提供的 CamWallet 日志：

```text
[Camera] id=0x5640 240x320 RGB565; frames=492 fps=2.7 capture=327ms lcd=38ms
```

该旧版样本中，采集函数（包含等待帧同步）约占串行采集与刷屏合计耗时的 **90%**。`327 + 38 = 365 ms` 对应约 `2.74 FPS`，与日志一致。

同日，开发者烧录本目录的官方 UF2 后，反馈画面更流畅。尚无官方程序的定量帧率日志，不能将主观对照写成具体 FPS。

当前优化代码已加入双缓冲流水采集、75 MHz SPI 请求，以及 `sensor`（相邻 VSYNC 间隔）、`wait`（主循环等待完成）日志，尚待开发者重新编译、烧录验证。操作与日志解释见 [板端指南](../README.md)。

旧版的 `capture` 含等待同步，流水版中又与上一帧刷屏重叠，不能用旧样本推算优化后的固定帧率上限。后续结合实际 `fps`、`sensor` 和画面稳定性决定是否需要调整传感器时序。
