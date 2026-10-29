# 板端固件操作指南

硬件：Waveshare RP2350-Touch-LCD-2-C，配套 OV5640 镜头与软排线，摄像头直接连接主板。

开发方式：C/C++、Raspberry Pi Pico SDK、VS Code 官方 Raspberry Pi Pico 扩展。

**当前固件已加入 quirc 扫码和 qrcodegen 原样回传测试，尚未实现钱包功能。** 摄像头预览、方向和二维码扫描已收到实物成功反馈；二维码模块通过电脑端 15 项集成测试。扫码循环实物约 8.1 FPS、识别耗时约 29 ms。`DIAG2` 日志区分未定位二维码、解码失败和成功；成功后显示回传码 30 秒，再自动恢复扫码。新增电脑端 Python 自动验收工具见第 6 节，真实电脑摄像头读回 PASS 待验证。

## 1. 工程位置

实际需要用 VS Code 打开的目录是 `firmware/CamWallet/`，其中包含 `CMakeLists.txt`。

当前电脑的完整路径：

```text
C:\Users\liuhu\Desktop\github\CamWallet\firmware\CamWallet
```

主要文件：

- `CamWallet/CamWallet.c`：程序入口，摄像头预览、扫码、回传码显示和 30 秒自动恢复扫码。
- `CamWallet/CMakeLists.txt`：编译目标、源文件、依赖库和 USB 日志配置。
- `CamWallet/drivers/board_pins.h`：本款 Waveshare 板的 LCD 和 OV5640 引脚。
- `CamWallet/drivers/lcd.c`、`lcd.h`：屏幕初始化、矩形和文字绘制。
- `CamWallet/ui/scan_preview.c`、`scan_preview.h`：在已完成的摄像头帧中合成上下状态条，然后一次刷屏，避免预览与文字反复覆盖导致闪动。
- `CamWallet/drivers/camera.c`、`camera.h`：OV5640 配置、异步启动/等待采集和 VSYNC 帧周期测量。
- `CamWallet/drivers/camera_capture.pio`：由 Pico SDK 的 pioasm 生成头文件的 DVP 采集程序。
- `CamWallet/qr/qr.c`、`qr.h`：RGB565 转灰度、quirc 识别、128 字节限制、qrcodegen 编码和二维码绘制。
- `CamWallet/third_party/quirc/`、`qrcodegen/`：已随仓库提供的第三方源码，编译时无需另外下载；固定版本见 `third_party/README.md`。
- `CamWallet/third_party/waveshare/`：官方示例提供的位图字体和摄像头初始寄存器表。
- `CamWallet/THIRD_PARTY_NOTICES.txt`：第三方来源及许可，分发固件时一并保留。
- `CamWallet/build/CamWallet.uf2`：编译成功后生成的 USB 烧录文件。
- `tests/camwallet-hello.svg`：用浏览器打开的测试二维码，内容为 `CamWallet:hello-12345`。
- `tests/`：独立电脑端 C 集成测试，不依赖板子，也不生成板端 UF2。

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
3. 本次新增了 `qr/` 和两个第三方库，首次更新后先执行 **Configure CMake**，再点击 **Compile Project**。后续按插件提示重新配置即可。
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

当前扫码版本启动时分配识别器、读取芯片 ID，并在预览期间约每秒输出一行统计。日志格式如下，省略号为待实测的数值；SPI 示例对应 150 MHz 外设时钟：

```text
[LCD] SPI=75000000 Hz
[CamWallet] QR ECHO TEST DIAG2
[LCD] preview: composed status bars, one frame transfer
[QR] scanner ready: 240x320 gray8, max_payload=128, response=30s
[Camera] chip_id=0x5640
[Camera] ready: 240x320 RGB565, pipelined capture, DMA32
[Camera] id=0x5640 240x320 RGB565; frames=... fps=... capture=...ms wait=...ms lcd=...ms qr=...ms sensor=...ms
[QR] scan: diag=2 candidates=0 decoded=0 gray=... status="NO QR FOUND" error="none" mirrored="none"
[QR] no successful decode yet
```

统计含义：

- `fps`：最近一个统计窗口内，实际完成 LCD 刷新的帧率。
- `capture`：从启动本帧 DMA 到主循环观察到采集完成的时间，包含等待 VSYNC；部分时间与上一帧刷屏重叠。
- `wait`：本次主循环等待采集完成的时间；下一帧已在上一轮刷屏前启动，所以通常小于 `capture`。
- `lcd`：本帧发送到屏幕并绘制扫码提示的时间。
- `qr`：当前帧转灰度、检测和尝试解码的时间，不含回传码保持的 30 秒。
- `qr` 有数值只表示执行过识别，并不表示扫码成功。新增 `[QR] scan` 每秒报告当前帧：`candidates` 是定位到的候选二维码网格数，`decoded` 是完成解码的数量，`gray` 是图像最小/最大灰度，`error` 与 `mirrored` 分别是正常和镜像重试的最近解码错误。
- `sensor`：摄像头相邻 VSYNC 上升沿的间隔，单位毫秒，与主循环独立测量；尚未收到两次边沿时为 0。可用 `1000 / sensor` 粗略估计传感器供帧速率。

**流水版本的 `capture`、`qr` 和 `lcd` 不能直接相加计算帧率**，因为下一帧采集与当前帧识别、刷屏重叠。判断流畅度应以 `fps` 为准，结合 `sensor` 区分传感器供帧与显示循环的限制。回传码显示期间暂停采集和预览统计，30 秒结束后重新计时。

启动日志可能在串口监视器打开之前已经输出，后续统计会重复包含芯片 ID。旧版的 `Hello, world!` 和 `[CamWallet] LCD update sent` 均已替换；若仍看到旧日志，应检查烧录版本。

### 一直停在扫码阶段时看哪里

- `candidates=0 decoded=0`、`NO QR FOUND`：尚未定位到可解码的二维码网格。先让自带测试码的白边和三个定位方块完整进入画面，调整距离、大小和反光，保持稳定。
- `candidates` 大于 0、`decoded=0`、`QR DECODE FAILED`：已找到候选图形，但解码失败。串口会显示如 `ECC failure` 或 `Format data ECC failure` 的具体原因，便于继续检查图像采样、清晰度和二维码完整性。
- `no successful decode yet`：这次启动后尚未成功读取过受支持的 payload。
- 成功会立即打印 `[QR] received`、`payload_hex`、`text`，不依赖屏幕回传编码是否成功。最近一次成功内容保留在 RAM，之后约每 5 秒用 `last success age=...` 和 `last result` 重复输出，扫码阶段和 30 秒回传阶段都有效；该记录表示历史结果。复位清除历史结果。
- 如果仍只有 `[Camera]` 行、没有 `diag=2`，运行的是诊断更新前的版本，重新编译并烧录本次 UF2。

## 6. 二维码扫描与屏幕回传操作

1. 需要插拔摄像头排线时，先断开 USB 和电池供电，再操作排线座；已经接好的摄像头保持连接即可。
2. 用 VS Code 打开 `firmware/CamWallet/`，保存文件，先执行 **Configure CMake**，再点击 **Compile Project**，生成包含二维码功能的新 UF2。
3. 按第 4 节的方法烧录本次生成的 `CamWallet.uf2`。
4. 启动时屏幕短暂显示 `CamWallet` 和 `STARTING CAMERA`。
5. 初始化成功后显示 **240×320 RGB565 彩色预览**，顶部根据结果显示 `NO QR FOUND`、`QR DECODE FAILED` 或拒绝原因，底部显示 `FOUND 0 READ 0` 等候选数与解码数；刚恢复扫码时可能短暂显示 `SCAN QR`。刚启动时等待曝光、白平衡稳定几帧。数据上限仍是 128 字节。
6. 在电脑浏览器中打开本仓库的 [tests/camwallet-hello.svg](tests/camwallet-hello.svg)，保持二维码和四周白边完整可见，不需要联网或安装二维码生成器。该码内容固定为 **`CamWallet:hello-12345`**，共 **21 字节**。
7. 将板子摄像头对准电脑上的测试码，调整距离和二维码显示大小，使三个定位方块与完整白边都进入画面，边缘尽量清晰。画面中只放一个二维码，避免反光、模糊和屏幕摩尔纹。
8. 识别成功后，板子切换到白底黑色二维码，上方显示 `QR ECHO / TEST`，下方显示 `21 BYTES ECI=0`，最底部显示 `RESCAN IN 30s` 并倒计时。回传的是原始数据字节，二维码图案本身可以与输入不同。
9. 可使用 [电脑端 Python 工具](../companion/README.md)自动读取板端屏幕并比较内容；固定测试码模式使用 `--text 'CamWallet:hello-12345'`。也可临时用能读取普通文本的扫码工具读回，结果必须完整等于 **`CamWallet:hello-12345`**，包含大小写、冒号和连字符。
10. 移开原始测试码，等待 **30 秒**，板子自动恢复预览和 `SCAN QR`。需要提前重扫时，只按一下 **RESET / RST** 即可，不要按 BOOT。触摸操作尚未接入；如果原始码仍在镜头前，恢复后会再次识别并显示同一回传码。

正常识别测试码时，USB 日志应包含：

```text
[QR] received: bytes=21 eci=0
[QR] payload_hex=43616d57616c6c65743a68656c6c6f2d3132333435
[QR] text="CamWallet:hello-12345"
[QR] echo ready: original payload bytes, hold=30s; RESET also rescans
[QR] resuming scanner
```

这只是二维码数据回传测试，不解析交易、不签名，也不进行录像或照片保存。此版未加载自动对焦固件。开发者已反馈扫码成功；电脑摄像头读回的一致性验收和长期稳定性继续验证。

### 使用电脑端工具自动验收（推荐）

板端已经运行扫码回传固件时，本步骤不需要修改或重新烧录固件。在仓库根目录的 PowerShell 中运行已准备好的虚拟环境：

```powershell
.\companion\.venv\Scripts\python.exe -X utf8 .\companion\qr_echo.py
```

1. 工具显示一张每轮内容不同的请求二维码；先让板端摄像头扫描它。
2. 等板端显示 `QR ECHO / TEST` 后，立即点击电脑窗口的“板子已显示回传码，开始读取”。
3. 电脑请求码会隐藏，随后打开电脑摄像头预览。将板端屏幕对准电脑摄像头。
4. 工具逐字节比较，显示 **PASS / FAIL** 和实际收到的内容；未读到时报告 TIMEOUT。PASS 才表示本轮电脑发码、板端回传和电脑读回全部通过。
5. 下一轮等待板子恢复扫码，或只按 RESET；电脑窗口点击“新一轮”。

默认请求为 31 字节，包含本轮随机值，避免将历史回传误认为本轮成功。电脑端读取前会隐藏原始请求码，防止摄像头直接扫到原码。摄像头选择、重建 `.venv`、依赖安装与详细排查见 [companion/README.md](../companion/README.md)。

### 支持范围和限制

- 每次处理一个 **单帧 QR Code**，数据长度为 **1–128 字节**，按字节计数，UTF-8 中文通常占多个字节。
- 支持数字、字母数字和 byte 模式；支持无 ECI、ECI 3（ISO-8859-1）和 ECI 26（UTF-8）。Kanji 模式和其他 ECI 显示 `QR MODE UNSUPPORTED`。
- 支持范围内按解码后的 payload 字节重新编码，不附加前缀或校验串，含零字节的数据也不会按 C 字符串截断。USB 文本日志会转义非 ASCII 字节，准确内容以 `payload_hex` 为准；手机对无 ECI 二进制或非 UTF-8 文本的显示取决于扫码工具。
- 超过 128 字节显示 `QR OVER 128 BYTES`，空内容显示 `QR IS EMPTY`；继续预览，不输出截断后的回传码。将其移开再换成受支持的二维码即可。
- 回传码使用至少 M 级纠错、整数缩放、每模块至少 3 个屏幕像素和四模块白色静区。二维码周围的文字和倒计时不会覆盖静区。
- 暂不做 UR 动态二维码分帧重组。扫描单个 UR 片段只会回传该片段文本，不代表已收到整份请求；二维码内容的业务格式也不会在本阶段校验。

### 内存和采集方式

保留两个各 **153600 字节**的静态彩色缓冲区，共 **307200 字节（300 KiB）**。完成一帧后，主循环立即向另一块缓冲启动采集，在稳定的已完成帧上提取灰度并识别，再刷新 LCD。quirc 启动时一次性分配 **76800 字节（75 KiB）**灰度图和识别工作区，扫描循环不反复创建识别器。quirc 的大结果结构放在静态区；使用单精度浮点配置。

扫描预览的上下状态条现在先在内存中合成，再随摄像头画面一次发送给 LCD。旧版每帧先铺满摄像头画面，再清空上下条并逐字写入文字，会造成状态条闪动。新流程在二维码识别完成后才修改当前帧，不影响识别输入，也不增加全屏缓冲；另一帧的 DMA 采集照常进行。更新后先执行 **Configure CMake → Compile Project**，按第 4 节重新烧录；启动日志中的 `composed status bars, one frame transfer` 可确认是本次版本。观察上下黑条和文字是否稳定，并完成一次扫码回传、等待恢复预览。2026-10-29 开发者已反馈闪动修复成功。

CMake 将主 SRAM 顶部的 **32 KiB** 单独划为主核栈，并相应缩小普通 RAM/堆区域，避免默认 4 KiB scratch bank 容纳不下解码栈。回传前先等待或超时中止未完成的 DMA，再复用已完成的彩色缓冲绘制二维码，不另外分配全屏缓冲。主核内存布局和静态内存由链接器检查，识别器分配失败会明确显示 `QR MEMORY ERROR`。

每帧仍重新同步 VSYNC，采集有从启动时计起的 2 秒超时。LCD SPI 请求为 75 MHz，启动日志输出实际频率；已验证的传感器与 LCD 方向设置保留。实物已收到 `fps=8.1 capture=122ms wait=68ms lcd=24ms qr=29ms sensor=122ms`，后续开发者反馈扫码成功；新增电脑摄像头的自动一致性验收仍需实际运行工具确认。

初始化失败时屏幕显示 `CAMERA ERROR` 和原因，USB 日志重复打印错误及芯片 ID。检查连接后重新复位。采集超时也会显示错误并输出剩余字节数、同步信号电平和 PIO 位置，程序会自动重试，成功后恢复预览。

### 官方摄像头 demo 对照

厂商资料包提供 `C/02-CAM/` 摄像头示例和预编译的 `02-CAM.uf2`。本地已将原始固件提取到 `official-demo/02-CAM.uf2`，操作步骤、来源校验和实现差异见 [官方 demo 对照说明](official-demo/README.md)。UF2 文件已由 Git 忽略；其他电脑需要从说明中的官方压缩包提取。

旧版已收到 `fps=2.7 capture=327ms lcd=38ms`，开发者随后烧录官方固件，反馈画面更流畅。双缓冲版本借鉴后台采集与刷屏重叠的方式，增加独立缓冲交接，并测量 VSYNC 帧间隔。后续扫码版实物反馈约 8.1 FPS；官方资料未提供此配置可保证的 FPS，扫码准确性另按第 6 节验收。

### 已处理的左右镜像问题

初版使用方向寄存器 `0x36` 的参数 `0x08`，实物反馈文字左右镜像。

现在 `drivers/lcd.c` 将参数调整为 **`0x48`**：翻转水平方向，保留上下方向及颜色顺序，实物已确认文字显示正常。

### 摄像头画面上下颠倒修正（已实物确认）

实物反馈为“上下颠倒，左右正常”。本次仅在 `drivers/camera.c` 的传感器预览配置中开启垂直翻转：`0x3820` 从 `0x01` 改为 **`0x07`**，设置传感器与 ISP 垂直翻转位，并保留原有垂直 binning 位。当前 binning 模式下，配套 `0x4514` 从 `0xaa` 调整为 `0xbb`，参考 [Espressif OV5640 驱动的 set_image_options](https://github.com/espressif/esp32-camera/blob/master/sensors/ov5640.c)。水平寄存器 `0x3821=0x00` 和 LCD 方向 `0x48` 保持不变。

方向调整由摄像头完成，无需在 CPU 上逐行翻转画面。开发者已反馈画面方向正常；新增扫码版仍需重新编译和烧录。

## 7. 常见问题

- **没有 RP2350 盘符**：重新执行 BOOT / RESET 顺序，并确认 USB 线支持数据传输。
- **烧录后盘符消失**：通常是正常重启；接下来观察屏幕或查看 COM 端口。
- **没有 COM 端口**：确认板子已经运行固件，而非仍停在 BOOTSEL 模式，并检查 USB 日志配置已启用。
- **串口无法打开**：检查 COM 端口是否变化，关闭其他占用该端口的串口工具后重试。
- **仍然输出 Hello, world! 或显示旧画面**：检查是否编译了正确工程、编译是否成功，以及复制的是否为该工程最新 UF2。
- **屏幕仍左右镜像**：确认源码中的方向参数为 `0x48`，并重新编译和烧录；若仍异常，反馈具体显示方向。
- **屏幕黑屏或花屏**：记录 USB 日志和屏幕现象，先核对固件版本，再排查 LCD 初始化与板级引脚。
- **扫码预览的上下黑条或文字反复闪动**：重新配置、编译并烧录包含 `composed status bars` 日志的版本；该版本将状态条和画面合成后一次刷屏。如果仍闪动，区分是上下文字区域还是整个摄像头画面亮暗变化，并记录串口统计。
- **本次提高 SPI 速度后出现花屏**：可将 `drivers/lcd.c` 中 `spi_init()` 的 `75000000u` 改回 `40000000u`，重新编译、烧录，对照是否与 SPI 速度有关；双缓冲采集仍可保留。
- **SCCB READ ERROR / WRONG SENSOR ID**：摄像头未正常响应或 ID 不符；断电后检查排线方向、插入位置及锁扣，恢复供电后复位。日志中的正确 ID 应为 `0x5640`。
- **REGISTER WRITE ERROR / SIZE VERIFY ERROR**：配置写入或尺寸回读失败；记录错误日志中的寄存器地址及返回值。
- **FRAME TIMEOUT**：寄存器初始化后未能及时收到完整画面；记录 `remaining`、`VSYNC`、`HREF`、`PCLK`、`PC` 日志，反馈用于检查 DVP 时序。程序会自动重试。
- **预览偏色、条纹、翻转或不清晰**：反馈具体画面现象和 USB 日志；这些需要实物确认像素格式、时序、传感器方向和对焦状态。
- **有预览但不识别二维码**：先用仓库自带测试 SVG；只保留一个二维码，保证白边完整，改变距离、屏幕亮度和显示大小，等画面稳定。此版没有自动对焦固件。保留包含 `fps`、`qr` 和 `sensor` 的 USB 日志，便于区分图像不清晰与处理耗时。
- **屏幕变成二维码后没有预览**：这是回传状态，保持 30 秒供手机读取；倒计时结束自动恢复，或只按 RESET 提前重启。
- **QR OVER 128 BYTES / QR MODE UNSUPPORTED / QR IS EMPTY**：输入超出当前范围，换成 1–128 字节、支持模式的单帧码；不支持的输入不会截断回传。
- **QR MEMORY ERROR**：识别器分配失败；确认执行了 Configure CMake 并使用本次内存布局设置，再编译烧录。保留日志，不要直接增大帧缓冲或恢复默认栈配置。
- **提示 quirc.h、qrcodegen.h 找不到，或旧版日志中没有 qr=**：确认拿到了新增 `qr/` 和 `third_party/` 文件，执行 Configure CMake，再编译并烧录本次 UF2。
- **编译失败**：保留终端最后一段错误信息，修复后再烧录。

## 8. 日志调试与断点调试

当前流程使用 USB 烧录和 USB 日志，足以进行屏幕、摄像头等模块的初步联调。

VS Code 的 **Debug Project** 断点、单步调试，以及 **Flash Project (SWD)** 烧录，需要外接兼容的 SWD 调试器，例如 Raspberry Pi Debug Probe，并连接板子的 SWD 接口。普通 USB 数据线不提供 SWD 调试能力。

## 9. 后续功能与文档维护

当前已加入单帧扫码与原样回传；下一步先按第 6 节确认实物读回内容一致，再加入 UR 分帧、触摸、EVM 交易解析与确认、密钥和签名。钱包阶段计划由板端处理助记词和私钥，采用使用期间保存在 RAM 的无状态设计；这些钱包功能目前尚未实现。

每次增加或修改可操作功能时，同步更新本文件的操作步骤、预期结果和常见问题，并区分“代码已加入”和“实物已验证”的状态。

## 10. 可选：在电脑上运行二维码集成测试

该测试使用主机 C 编译器，不使用 Arm 编译器，不连接板子，不替代实物扫码。在仓库根目录运行，环境需要 CMake、Ninja 和主机 GCC/Clang：

```powershell
cmake -S firmware/tests -B firmware/tests/build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build firmware/tests/build
ctest --test-dir firmware/tests/build --output-on-failure
```

直接运行 `firmware/tests/build/qr_roundtrip_test.exe` 可看到 15 项详细结果：普通文本、旋转、镜像、彩色 RGB565 输入、含零字节的二进制、UTF-8/Latin-1 ECI、128 字节边界、超长/空值/不支持 ECI 拒绝，以及输入和渲染边界。测试会将输入渲染成 RGB565 后实际识别，再对固件生成的回传画面重新识别，比较长度、ECI 和全部 payload 字节，并检查帧缓冲边界保护字节。新增损坏二维码测试，确认“已定位但解码失败”会返回明确状态和错误信息，并验证下一帧不会沿用旧诊断。

本次电脑端这些测试已通过，板端 C 语法检查已通过；板端完整固件编译、烧录和光学回传由开发者按前文步骤验证。

`ctest` 还包含 `scan_preview` 状态条合成回归测试，检查摄像头画面改变时状态条像素保持一致、中间预览不被改写、长文字裁剪、旧文字清除及缓冲区边界保护。该测试已通过，开发者也已反馈实物闪动修复成功。

## 参考资料

- [Raspberry Pi Pico 官方 VS Code 扩展](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico)
- [Pico C/C++ SDK 入门](https://www.raspberrypi.com/documentation/microcontrollers/c_sdk.html)
- [Debug Probe 与 SWD 调试](https://www.raspberrypi.com/documentation/microcontrollers/debug-probe.html)
- [Microsoft Serial Monitor](https://github.com/microsoft/vscode-serial-monitor)
- [Waveshare 板卡资料](https://www.waveshare.com/wiki/RP2350-Touch-LCD-2)
