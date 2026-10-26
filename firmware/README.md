# 板端固件操作指南

硬件：Waveshare RP2350-Touch-LCD-2-C，配套 OV5640 镜头与软排线，摄像头直接连接主板。

开发方式：C/C++、Raspberry Pi Pico SDK、VS Code 官方 Raspberry Pi Pico 扩展。

**当前固件是 LCD 与 USB 日志测试程序，尚未实现钱包功能。** USB 日志和屏幕点亮已经在实物上验证；左右镜像修正已写入代码，需烧录修正后的版本核对方向。

## 1. 工程位置

实际需要用 VS Code 打开的目录是 `firmware/CamWallet/`，其中包含 `CMakeLists.txt`。

当前电脑的完整路径：

```text
C:\Users\liuhu\Desktop\github\CamWallet\firmware\CamWallet
```

主要文件：

- `CamWallet/CamWallet.c`：程序入口，绘制测试页、更新运行秒数、输出 USB 日志。
- `CamWallet/CMakeLists.txt`：编译目标、源文件、依赖库和 USB 日志配置。
- `CamWallet/drivers/board_pins.h`：本款 Waveshare 板的 LCD 引脚。
- `CamWallet/drivers/lcd.c`、`lcd.h`：屏幕初始化、矩形和文字绘制。
- `CamWallet/third_party/waveshare/`：官方示例提供的位图字体。
- `CamWallet/THIRD_PARTY_NOTICES.txt`：第三方来源及许可，分发固件时一并保留。
- `CamWallet/build/CamWallet.uf2`：编译成功后生成的 USB 烧录文件。

## 2. 第一次配置开发环境

1. 安装 VS Code。
2. 在扩展面板搜索并安装 **Raspberry Pi Pico**，发布者为 **Raspberry Pi**。
3. 选择“文件 → 打开文件夹”，打开上面的 `firmware/CamWallet/`。
4. 如果插件提示导入现有 Pico 工程，选择 **Import Project**，使用这个目录。仓库已经有工程，不需要重新创建一份。
5. 使用插件管理 SDK 和编译工具，等待所需组件下载完成。

现有工程记录的工具版本：

- Pico SDK：`2.3.1`。
- Arm 工具链：`15_2_Rel1`。
- picotool：`2.3.1`。
- Board type：`Pico 2` / `pico2`。
- 架构：Arm Cortex-M33，**Use RISC-V 不勾选**。
- **Console over USB 勾选**，**Console over UART 不勾选**。

这些版本由插件生成的 `CMakeLists.txt` 记录，日常开发不必修改其中标有 `DO NOT EDIT` 的部分。

当前 `pico2` 配置用于 RP2350 启动与基础编译；屏幕使用 `drivers/board_pins.h` 中的专用引脚。独立板型及完整 16 MB Flash 配置仍待完善，不能将 Pico 2 的默认外设引脚直接套用到这块板。

## 3. 编译固件

1. 在 VS Code 中保存修改过的文件。
2. 点击左侧 **Raspberry Pi Pico** 图标。
3. 点击 **Compile Project**。若插件要求先配置工程，先完成 **Configure CMake**。
4. 等待终端中的编译任务成功结束。
5. 在工程 `build/` 下找到 `CamWallet.uf2`。

当前电脑的烧录文件路径：

```text
C:\Users\liuhu\Desktop\github\CamWallet\firmware\CamWallet\build\CamWallet.uf2
```

**确认本次编译成功后再烧录。** 编译失败时，`build/` 中可能仍留着上次成功生成的旧 UF2。

编译、烧录由开发者在 VS Code 和文件资源管理器中手动执行。

## 4. 通过 USB 烧录

板子已经连接电脑时：

1. 如果串口监视器正在运行，先停止监视。
2. 按住板上的 **BOOT** 按键不放。
3. 按一下 **RESET / RST**，然后松开 RESET，BOOT 继续按住。
4. 松开 BOOT。
5. 按 `Win + E` 打开文件资源管理器，在“此电脑”中找到 **RP2350** 盘符。
6. 将本次编译生成的 `CamWallet.uf2` **复制到 RP2350 盘的根目录**。
7. 复制结束后，盘符通常会自动消失，板子重启并运行新程序。

另一种进入方式：断开 USB，按住 BOOT，重新插入支持数据传输的 USB 线，再松开 BOOT。

BOOTSEL 模式用于接收 UF2；运行固件时不要求 RP2350 盘符继续存在。烧录会替换板上原来的演示程序。

## 5. 查看 USB 日志

1. 在 Windows 设备管理器中展开 **端口（COM 和 LPT）**。
2. 找到板子对应的 **USB 串行设备（COMx）**，记下端口号；重启或重新插拔后端口号可能变化。
3. 在 VS Code 中安装微软的 **Serial Monitor** 扩展。
4. 打开底部 **Serial Monitor** 面板，选择对应 COM 端口。
5. 波特率选择 **115200**，点击 **Start Monitoring**。

当前屏幕测试版本每秒输出类似内容：

```text
[CamWallet] LCD update sent; uptime=1 seconds
[CamWallet] LCD update sent; uptime=2 seconds
[CamWallet] LCD update sent; uptime=3 seconds
```

早期生成工程中的 `Hello, world!` 已被上述日志替换。日志表示程序发送了 LCD 更新，实际显示效果仍需要观察屏幕确认。

## 6. 屏幕测试的预期结果

正常运行时，240×320 竖屏应显示：

- `CamWallet` 标题。
- `LCD READY`、`RP2350 / 240x320`、`USB LOG ACTIVE`。
- `UPTIME` 及持续增加的运行秒数。
- 从左到右排列的红、绿、蓝色块。
- 底部的 `DISPLAY TEST`。

当前版本只接入了 LCD，触摸和摄像头尚未启用。

### 已处理的左右镜像问题

初版使用方向寄存器 `0x36` 的参数 `0x08`，实物反馈文字左右镜像。

现在 `drivers/lcd.c` 将参数调整为 **`0x48`**：翻转水平方向，保留上下方向及颜色顺序。修改源码后，需要重新编译并烧录新 UF2 才会生效。

## 7. 常见问题

- **没有 RP2350 盘符**：重新执行 BOOT / RESET 顺序，并确认 USB 线支持数据传输。
- **烧录后盘符消失**：通常是正常重启；接下来观察屏幕或查看 COM 端口。
- **没有 COM 端口**：确认板子已经运行固件，而非仍停在 BOOTSEL 模式，并检查 USB 日志配置已启用。
- **串口无法打开**：检查 COM 端口是否变化，关闭其他占用该端口的串口工具后重试。
- **仍然输出 Hello, world! 或显示旧画面**：检查是否编译了正确工程、编译是否成功，以及复制的是否为该工程最新 UF2。
- **屏幕仍左右镜像**：确认源码中的方向参数为 `0x48`，并重新编译和烧录；若仍异常，反馈具体显示方向。
- **屏幕黑屏或花屏**：记录 USB 日志和屏幕现象，先核对固件版本，再排查 LCD 初始化与板级引脚。
- **编译失败**：保留终端最后一段错误信息，修复后再烧录。

## 8. 日志调试与断点调试

当前流程使用 USB 烧录和 USB 日志，足以进行屏幕、摄像头等模块的初步联调。

VS Code 的 **Debug Project** 断点、单步调试，以及 **Flash Project (SWD)** 烧录，需要外接兼容的 SWD 调试器，例如 Raspberry Pi Debug Probe，并连接板子的 SWD 接口。普通 USB 数据线不提供 SWD 调试能力。

## 9. 后续功能与文档维护

后续按顺序加入触摸、摄像头采集、二维码收发、EVM 交易解析和签名。钱包阶段计划由板端处理助记词和私钥，采用使用期间保存在 RAM 的无状态设计；这些功能目前尚未实现。

每次增加或修改可操作功能时，同步更新本文件的操作步骤、预期结果和常见问题，并区分“代码已加入”和“实物已验证”的状态。

## 参考资料

- [Raspberry Pi Pico 官方 VS Code 扩展](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico)
- [Pico C/C++ SDK 入门](https://www.raspberrypi.com/documentation/microcontrollers/c_sdk.html)
- [Debug Probe 与 SWD 调试](https://www.raspberrypi.com/documentation/microcontrollers/debug-probe.html)
- [Microsoft Serial Monitor](https://github.com/microsoft/vscode-serial-monitor)
- [Waveshare 板卡资料](https://www.waveshare.com/wiki/RP2350-Touch-LCD-2)
