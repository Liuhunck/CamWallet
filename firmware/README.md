# 板端固件操作指南

硬件：Waveshare RP2350-Touch-LCD-2-C，配套 OV5640 镜头与软排线，摄像头直接连接主板。

开发方式：C/C++、Raspberry Pi Pico SDK、VS Code 官方 Raspberry Pi Pico 扩展。

**当前固件已加入仅在 RAM 中创建账户、原生币 EVM 审核和 Sepolia 签名。没有账户恢复或持久化保存。** 第 6a 节保留无密钥测试（开机选 TEST ONLY）；第 6b 节是 CREATE → 准备交易 → 扫码签名 → 主动广播的操作步骤。已通过 RP2350 编译及软件回归；新增账户和签名功能仍待实物与上链验收。

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
- `CamWallet/transport/`：cUR 收发会话、30 秒收集超时、分片/消息上限，以及独立 16 KiB 内存区。
- `CamWallet/evm/`：ERC-4527 请求 CBOR 和 EIP-1559 原生币转账的只读解析与校验。
- `CamWallet/third_party/cur/`：固定版本的 cUR 传输层源码，不含 BTC 类型编解码器。
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
3. 本次新增了 `transport/`、`evm/` 和 cUR，首次更新后先执行 **Configure CMake**，再点击 **Compile Project**。后续按插件提示重新配置即可。
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
[CamWallet] SEPOLIA SIGNER V1 / RAM ACCOUNT
[UR] cUR ready: part=128 message=768 fragments=32 arena=16384 timeout=30000ms
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
- 如果没有 `[QR] scan`，运行的是诊断更新前的版本；如果只有 `QR ECHO TEST DIAG2` 启动标识，则尚未烧录 cUR 版。重新编译并烧录本次 UF2。

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
10. 移开原始测试码，等待 **30 秒**，板子自动恢复预览和 `SCAN QR`。需要提前重扫时，只按一下 **RESET / RST** 即可，不要按 BOOT。普通二维码回传页没有触摸按钮；如果原始码仍在镜头前，恢复后会再次识别并显示同一回传码。

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

- 普通回传每次处理一个 **单帧 QR Code**，数据长度为 **1–128 字节**，按字节计数，UTF-8 中文通常占多个字节。以 `ur:` 开头（不区分大小写）的内容进入第 6a 节的 UR 处理，不再当作普通文本回传。
- 支持数字、字母数字和 byte 模式；支持无 ECI、ECI 3（ISO-8859-1）和 ECI 26（UTF-8）。Kanji 模式和其他 ECI 显示 `QR MODE UNSUPPORTED`。
- 支持范围内按解码后的 payload 字节重新编码，不附加前缀或校验串，含零字节的数据也不会按 C 字符串截断。USB 文本日志会转义非 ASCII 字节，准确内容以 `payload_hex` 为准；手机对无 ECI 二进制或非 UTF-8 文本的显示取决于扫码工具。
- 超过 128 字节显示 `QR OVER 128 BYTES`，空内容显示 `QR IS EMPTY`；继续预览，不输出截断后的回传码。将其移开再换成受支持的二维码即可。
- 回传码使用至少 M 级纠错、整数缩放、每模块至少 3 个屏幕像素和四模块白色静区。二维码周围的文字和倒计时不会覆盖静区。
- UR 分片必须收齐并通过整包 CRC 校验才会进入业务处理；重复分片不增加已恢复计数，不会回传单个分片。当前支持 `ur:bytes` 测试和 `ur:eth-sign-request` 的受限解析。

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

## 6a. cUR 动态二维码与 EVM 触摸审核

cUR 只负责 UR 外层、Bytewords、Fountain 分帧和重组；它不决定支持哪条链。本项目没有引入 cUR 的 PSBT、HD key 等 BTC 类型代码，EVM 请求由独立 C 模块处理。无需 Rust、Zephyr 或额外板端构建工具，相关源码随仓库提供。

工程检测到 `companion/.venv` 时，SDK 构建辅助脚本也优先使用该虚拟环境中的 Python，防止 Pico 扩展把解释器重新切回全局安装；没有此环境的电脑仍可使用 Pico 扩展配置的解释器完成 C 固件构建。

### 第一步：编译并烧录

1. VS Code 打开 `firmware/CamWallet/`，执行 **Configure CMake → Compile Project**。
2. 成功后按第 4 节烧录 `firmware/CamWallet/build/CamWallet.uf2`。
3. 开机选择左下 `TEST ONLY`（不创建账户），串口启动标识应为 `SEPOLIA SIGNER V1 / RAM ACCOUNT`，触摸日志应有 `[Touch] CST816D id=0xb6 ... READY`。USB 连接较晚可能错过启动日志，可打开串口后复位。普通测试 SVG 和 `companion/qr_echo.py` 仍可使用。

本机已生成上述路径的新版 UF2，**未自动烧录**。不要使用 `firmware/build/` 中旧实验目录的 UF2。

### 第二步：先测普通动态 UR

在仓库根目录的 PowerShell 中运行，继续使用已有虚拟环境；新增签名工具和完整测试所需依赖按第 6b 节同步：

```powershell
.\companion\.venv\Scripts\python.exe -X utf8 .\companion\ur_echo.py
```

1. 电脑每 400 ms 换一帧，默认发送 256 字节随机测试数据，CBOR 后为 259 字节、11 个基础分片。每轮内容不同。
2. 板子对准电脑二维码。顶部显示 `UR RECEIVING`，底部如 `UR 4/11 30s LIMIT`；这里的数字是已恢复的不同分片数，不是扫到的总帧数。混合帧可能暂时不增加这个数字。
3. 收齐后停止摄像头采集，显示 `UR ECHO / TEST` 动态回传。回传同样每 400 ms 更新，保持 60 秒。状态文字和二维码合成后一次刷屏。
4. 立即点击电脑工具的“板子已显示回传码，开始读取”。工具先隐藏请求二维码，再打开电脑摄像头。
5. 把板端屏幕对准电脑摄像头，等工具收齐回传并显示 **PASS**。比较的是完整的 UR 类型与 CBOR 字节，不要求分帧顺序或二维码图案相同。
6. 下一轮等待板子恢复扫码，或只按 RESET。电脑点“新一轮”。

可以用 `--camera 1` 选择相机，用 `--interval 600` 放慢电脑发码，或用 `--bytes 765` 测试整包 768 字节上限。间隔支持 200–2000 ms；大包放慢后可能超过板端 30 秒收集期限，需要恢复默认速度或减小数据长度。

正常日志示例：

```text
[UR] UR RECEIVING recovered=4/11 arena=.../16384
[UR] complete: type=bytes cbor=259 arena_peak=.../16384
[UR] response ready: type=bytes cbor=259 parts=11 interval=400ms hold=60s; NOT A SIGNATURE
[UR] echo frame=1 arena_peak=.../16384
[UR] resuming scanner; arena_used=0
```

### 第三步：触摸审核原生币转账

```powershell
.\companion\.venv\Scripts\python.exe -X utf8 .\companion\ur_echo.py --evm
```

此模式发送带随机 UUID 的 `ur:eth-sign-request`：Chain ID `11155111`（Sepolia），nonce `0`，收款地址 `0x1111111111111111111111111111111111111111`，金额 `1000000000000000 wei`，gas limit `21000`，max fee `2000000000 wei/gas`，priority fee `1000000000 wei/gas`。这些是离线固定测试参数，工具不查询 RPC、不签名、不广播。

1. 板子收齐请求并校验后，停止摄像头采集，显示 **`REVIEW 1/5`**。移开请求码，在板端核对内容。审核没有倒计时，也不会自动确认或自动回传；RESET 会中止本轮且不返回决定。
2. 轻点后松开右下角 **NEXT** 翻页，右上角 **BACK** 返回上一页。第一页没有 BACK。每次须松手后再点，长按不会连续翻页。五页内容依次为：
   - **1/5 网络**：网络名、完整 Chain ID、nonce、Type 2。已标注 Ethereum mainnet 和 Sepolia；其他链显示 `UNKNOWN NETWORK` 和真实链号，不猜原生币符号或精度。
   - **2/5 地址**：带 `0x` 的完整 40 位十六进制收款地址，跨行顺序拼接，无省略号。可选发送地址标为 `UNVERIFIED`，设备没有密钥证明它属于自己。
   - **3/5 金额**：完整最小单位整数，支持 78 位 uint256。Ethereum/Sepolia 另显示精确 18 位小数的 ETH 数量，不使用浮点数。默认示例为 `1000000000000000 wei`，即 `0.001000000000000000 ETH`。
   - **4/5 费用**：最高总费用、gas limit、max fee/gas、priority fee/gas。默认最高费用是 `42000000000000 wei`（`21000 × 2000000000`），这是上限，不是实际扣费。
   - **5/5 决定**：完整请求 UUID，以及 `NO SIGNATURE / NO BROADCAST`。仅此页出现 **CONFIRM**。
3. **确认测试**：依次看完各页，在第 5 页轻点并松开右下 **CONFIRM**。板端显示 `CONFIRMED / NO SIGN` 动态回传码。再点击电脑的“板子已显示回传码，开始读取”，把板端屏幕对准电脑摄像头。预期 **PASS：板端已确认 (CONFIRMED)**，并显示请求编号、摘要一致。
4. **拒绝测试**：等 60 秒回到扫码，或按 RESET；电脑点“新一轮”。重新扫描后，在任意审核页轻点并松开左下 **REJECT**。板端显示 `REJECTED / NO SIGN`；电脑读回后预期 **PASS：板端已拒绝 (REJECTED)**。这里 PASS 表示拒绝回执与本轮请求匹配，不表示交易被批准。
5. 两种回传均每 400 ms 换帧，保持 60 秒，之后恢复扫码。电脑只有在点击“开始读取”后才启动自己的读取超时，因此可先从容完成审核。不要用上一轮回执验收新一轮：UUID 或请求摘要不一致会 FAIL。

**首次实物验收**：先试 NEXT、BACK 和 REJECT，确认点的位置与动作相符，再做确认测试。按住 NEXT 超过 2 秒应不翻页；放开后重新轻点才有效；审核页停留超过 10 秒不应自动回传。审核内容只在翻页时合成并整屏发送，静止时不反复清屏。

预期串口结果（UUID 每轮不同）：

```text
[EVM] REVIEW ONLY request_id=... chain_id=11155111 nonce=0
[EVM] to=0x1111111111111111111111111111111111111111 value_wei=1000000000000000 gas_limit=21000
[EVM] max_fee_per_gas=2000000000 priority_fee_per_gas=1000000000 max_total_fee_wei=42000000000000
[EVM] path=m/44'/60'/0'/0/0; sender=UNVERIFIED / NO KEY
[EVM] review page=1/5; waiting for manual touch; no automatic response
[Touch] down x=180 y=290 page=1
[EVM] review page=2/5
...
[EVM] decision=CONFIRMED request_id=...; signature not generated yet
[UR] response ready: type=bytes cbor=57 parts=3 interval=400ms hold=60s; NOT A SIGNATURE
```

拒绝时对应日志为 `decision=REJECTED`。串口仍输出完整交易字段，便于与板端页面核对。

当前请求子集：必须提供 tag 37 的 16 字节 request UUID，`dataType=4`；派生路径为 tag 304 的 `m/44'/60'/0'/0/0`。`sign-data` 必须是 `0x02 || RLP([chain_id, nonce, max_priority_fee_per_gas, max_fee_per_gas, gas_limit, to, value, data, access_list])`，正好 9 个字段，不含签名槽位。仅接受 20 字节收款地址、空 data 和空 access list。链号须为正数且与请求元数据一致；元数据省略时默认链号 1。链号、nonce、每 gas 费用限制为 uint64，gas limit 为 uint32 且至少 21000，金额支持完整 uint256。origin 若提供，仅接受最多 32 字节可打印 ASCII。拒绝重复/未知字段、非最短 CBOR/RLP、尾随数据及不支持类型。

本节 TEST ONLY 模式没有加载密钥，地址字段只做长度检查，不能证明属于设备；source fingerprint 只校验结构。**CONFIRM 仅确认本轮审核，不签名、不广播，也不会输出 `eth-signature`。** 不同 Chain ID 走同一个 EVM 解析器，尚不等同于已经支持各链钱包连接。USDC/USDT 等 ERC-20 转账含 calldata，留待下一步。

### 触摸接线、故障与回执格式

触摸配置来自 [Waveshare 官方资料包](https://files.waveshare.com/wiki/RP2350-Touch-LCD-2/RP2350-Touch-LCD-2.zip) 的 `C/01-LCD/lib/Touch/CST816D.*`、`Config/DEV_Config.h`。芯片 CST816D，地址 `0x15`、ID 寄存器 `0xa7=0xb6`；I2C0 SDA=GP12、SCL=GP13、INT=GP29。**触摸和 LCD 共用 GP20 复位**，因此先由 LCD 初始化执行共享复位，触摸初始化不再次拉低 GP20。驱动关闭自动睡眠，审核期间每 20 ms 轮询；每次 I2C 操作最多等 3 ms，不在中断里访问 I2C。

当前 LCD 为 `MADCTL=0x48` 的 240×320 竖屏，使用原始 `(x,y)`；没有照搬官方横屏例程的 `(y,240-x)`。实际坐标仍需板上验证：右下按钮中心约 `(180,290)`，左下约 `(55,290)`，BACK 约 `(198,20)`。串口 `[Touch] down x=... y=...` 用于核对。若方向不符或点右边却触发左边，停止确认测试并记录这三个位置的坐标；不要修改已经验证正常的摄像头方向。

进入审核后先要求无手指稳定 60 ms；按下至少 40 ms、至多 2 秒，且始终留在同一按钮，松手稳定 60 ms 才触发一次。跨按钮拖动、多点/越界报告、I2C 失败都会取消当前手势。触摸初始化失败或连续 10 次报告读取失败会显示 `TOUCH ERROR / NO RESPONSE SENT / RESET TO RETRY`，5 秒后回到扫描；**不会把读取错误当成松手确认，也不会伪造拒绝结果**。复位重试并检查日志。

回传沿用 `ur:bytes`，CBOR 是一个 **55 字节的 byte string**，编码后共 **57 字节、3 个基础分片**。内部固定格式为 `ASCII("CWREV") || version(0x01) || decision(0x00=拒绝,0x01=确认) || request UUID(16 B) || SHA-256(32 B)`。摘要覆盖重组后的**完整原始请求 CBOR 字节**，不含 UR 文本包装；UUID 保持请求原始字节顺序。电脑严格检查长度、版本、状态、UUID 和摘要。旧固件包裹原请求的回传会 FAIL，需要同时更新板端和电脑端。摘要用于绑定本轮内容，这种回执**没有数字签名或设备身份认证**。

### 限制、超时与排查

- **单张二维码最多 128 ASCII 字符**，整包 CBOR 最多 768 字节，最多 32 个基础分片。电脑工具使用每片最多 24 个原始字节；普通钱包默认生成的超大 QR 需要调小分片，当前不保证直接兼容。
- 首个有效分片开始计时，30 秒后清除未完成会话；重复码和其他消息不会延长期限。换请求前按 RESET，或移开旧码并等待超时。
- 一个会话只接收同一类型、同一消息头的分片；普通文本、其他消息和单帧 UR 不会替换正在接收的多帧消息。终止错误或超时后重新扫描。
- `UR FRAME REJECTED` 通常是混入其他码或内容损坏，继续扫描当前请求；`UR TOO LARGE` 是大小/片数超限；`UR MEMORY LIMIT` 是固定内存区不足，会释放接收状态后重试，可减小测试包。
- `EVM ...` 错误表示请求完整但业务校验不通过，不生成回传。`EVM DATA UNSUPPORTED` 表示含 calldata 或 access list；`EVM CHAIN MISMATCH` 表示链号不一致。
- 长时间停在 `0/N` 或部分计数时，检查完整白边、距离、反光与发码速度。中途错过基础帧后会继续使用 Fountain 混合帧恢复，进度不一定线性增长。

### 触摸审核版历史编译与内存结果

以下是加入签名前的基线；当前签名版数据见第 6b 节末尾。

2026-10-30，本机 Pico SDK 2.3.1、GCC 15_2_Rel1、Release、`pico2`/`rp2350-arm-s` 完整编译成功。与提交 `0a7addd` 的扫码回传基线对比：

| 项目 | 基线 | cUR + EVM 触摸审核版 | 增量 |
| --- | ---: | ---: | ---: |
| Flash 实际地址跨度 | 100528 B | 134804 B（131.6 KiB） | 34276 B |
| 静态 RAM：向量表 + data + bss | 330124 B | 347532 B（339.4 KiB） | 17408 B |
| 独立主核栈预留 | 32768 B | 32768 B | 0 |

静态 RAM 已包含 300 KiB 双帧缓冲和 cUR 的 **16 KiB 固定内存区**；cUR 不从 quirc 的系统堆分配。quirc 初始化申请的灰度图、对象、flood-fill 工作区合计约 92372 B（90.2 KiB），未含 malloc 元数据。扣除它们后，主 SRAM 到 `__HeapLimit` 的理论堆余量约 51616 B（50.4 KiB）；这不是实测最低剩余内存或栈高水位。相对上一版解析诊断，触摸审核新增 Flash 5620 B、静态 RAM 8 B；复用停止采集后的摄像头帧绘图，没有新增全屏缓冲。

Flash 使用 ELF 的 `__flash_binary_end - __flash_binary_start`，静态 RAM 使用各 RAM 段；不要把 UF2 文件大小当作 Flash 占用，也不要直接采用 `size` 默认的 `text` 列，它在此链接脚本下还计入栈/堆保留段。复核命令：

```powershell
& "$env:USERPROFILE/.pico-sdk/toolchain/15_2_Rel1/bin/arm-none-eabi-size.exe" -A firmware/CamWallet/build/CamWallet.elf
& "$env:USERPROFILE/.pico-sdk/toolchain/15_2_Rel1/bin/arm-none-eabi-nm.exe" -n firmware/CamWallet/build/CamWallet.elf | Select-String '__flash_binary|__bss_|__data_|__Heap|__Stack'
```

## 6b. 创建账户并在 Sepolia 签名

本轮只增加 **创建账户**，没有恢复、助记词输入或持久化保存。账户密钥只在 RAM：**RESET、断电或重新烧录后无法恢复原账户**，重新点 CREATE 会得到新地址。先用少量 Sepolia 测试 ETH 联调，保持板子供电到交易完成；不用它保存资产。

### 1. 更新并创建账户

1. 在 `firmware/CamWallet/` 执行 **Configure CMake → Compile Project**，按第 4 节烧录 `build/CamWallet.uf2`。新版启动标识为 `SEPOLIA SIGNER V1 / RAM ACCOUNT`。本机已编译生成，未自动烧录。
2. 开机进入 `SEPOLIA TEST WALLET`。点右下 **CREATE** 创建新账户；左下 **TEST ONLY** 进入原来的无密钥扫码/审核测试，第 6a 节仍可使用。没有恢复入口。
3. 创建后屏幕显示 **SEPOLIA ADDRESS** 地址二维码，内容是完整的 `0x...` 地址。用手机扫码查看/复制，或在 USB 串口取 `[Wallet] SEPOLIA address=...`。串口不输出种子或私钥。
4. 给这个地址转入少量 Sepolia 测试 ETH；可从 [Ethereum 官方网络页面](https://ethereum.org/developers/docs/networks/#sepolia) 的水龙头列表获取。这里需要测试 ETH 支付 gas。
5. 记录地址后轻点屏幕底部左侧或右侧，进入摄像头扫码。**本次供电期间账户不变**，后续交易等待回传倒计时结束即可重扫，别用 RESET 跳过倒计时。

预期日志：

```text
[CamWallet] SEPOLIA SIGNER V1 / RAM ACCOUNT
[Touch] CST816D id=0xb6 ... READY ...
[Wallet] SEPOLIA address=0x... path=m/44'/60'/0'/0/0; RAM ONLY, reset loses key
```

### 2. 电脑准备交易

在仓库根目录 PowerShell 运行。仍用原有 `.venv`；新增 `eth-account` 用于独立验签，其依赖已锁定：

```powershell
uv pip sync companion/requirements.txt --python companion/.venv/Scripts/python.exe

# 填你使用的 Sepolia HTTPS RPC 地址；仅保存在当前终端环境。
$env:CAMWALLET_RPC_URL = 'https://你的Sepolia-RPC地址'
$sender = '0x板端刚生成的完整地址'
# 首次可向自己转账；也可换成另一个 Sepolia 普通账户地址。
$recipient = $sender

.\companion\.venv\Scripts\python.exe -X utf8 .\companion\evm_wallet.py prepare --sender $sender --to $recipient --amount-eth 0.000001 --out companion/local/request1.json
```

`prepare` 校验 RPC 链号必须为 **11155111**，查询 pending nonce、base fee、priority fee、估算 gas 和余额，保存未签名参数。最高每 gas 费用为 `2 × base fee + priority fee`，板端审核时显示实际请求数值。首版只向无合约代码的普通账户转账；不支持 ERC-20、合约调用或非空 access list。金额按十进制精确转成 wei，最多 18 位小数。

输出文件不覆盖旧文件。再次准备请换 `request2.json` 等路径；`companion/local/` 已被 Git 忽略。RPC 只由电脑访问，板端不联网。

### 3. 扫码、确认签名、电脑验签

```powershell
.\companion\.venv\Scripts\python.exe -X utf8 .\companion\evm_wallet.py sign companion/local/request1.json --out companion/local/signed1.json
```

1. 让板子扫描电脑动态二维码。电脑每轮生成新 UUID；请求带完整 sender，板端要求与自己派生的地址一致。
2. 板端审核页显示 **SEPOLIA SIGNING**，地址页显示 **FROM (DEVICE KEY)**，最后一页显示 **SIGN THIS TRANSFER? / CONFIRM WILL SIGN**。核对网络、完整收付款地址、金额、nonce 和最高费用。
3. 点 **CONFIRM** 并松手后，板端对刚才审核的原始 Type 2 交易进行 Keccak-256 和 secp256k1 ECDSA 签名，再自验签名。随后显示 **SIGNED / SEPOLIA**。
4. 电脑点“板子已显示回传码，开始读取”，将板端屏幕对准电脑摄像头。需要换相机可加 `--camera 1` 或 `--backend dshow`。
5. 电脑收齐 `ur:eth-signature` 后，检查 UUID、r/s/v 范围、low-S，独立恢复发送地址，并组装 Type 2 原始交易、再次恢复和核对所有交易字段。只有通过才写入 `signed1.json`，显示 **PASS / 尚未广播** 和交易哈希。扫码本身不发送 RPC。
6. 在任意页点 **REJECT** 会返回绑定原请求的拒绝回执，电脑显示“已拒绝”，**不生成签名文件**。旧审核版的 CONFIRMED 回执也不能当作签名通过。

板端预期日志：

```text
[EVM] REVIEW TO SIGN request_id=... chain_id=11155111 nonce=...
[EVM] path=m/44'/60'/0'/0/0; sender=VERIFIED
[EVM] decision=CONFIRMED request_id=...; signature not generated yet
[UR] response ready: type=eth-signature cbor=89 parts=4 interval=400ms hold=60s; SIGNED / NOT BROADCAST
```

签名回传保持 60 秒，之后回到扫码，账户仍在 RAM。若电脑读取超时，等待板端回到扫描后重新运行 `sign`，保持供电；“新一轮”会换 UUID，但不会重新查询 nonce/费用。交易参数需要更新时重新运行 `prepare`。保存成功后先关闭签名窗口，再进入下一步。

### 4. 主动广播并查询上链结果

```powershell
.\companion\.venv\Scripts\python.exe -X utf8 .\companion\evm_wallet.py broadcast companion/local/signed1.json

# 将下面的参数替换成上一步打印的完整交易哈希。
.\companion\.venv\Scripts\python.exe -X utf8 .\companion\evm_wallet.py receipt 0x完整交易哈希
```

只有 `broadcast` 命令会调用 `eth_sendRawTransaction`。广播前再次校验文件中的签名、发送地址、交易字段、交易哈希、RPC 链号与 pending nonce；nonce 变化会停止，要求重新准备和审核。广播后打印 Sepolia 浏览器链接；`receipt` 显示“执行成功”且 `status=0x1` 才表示成功上链，PASS 只代表本地验签通过。

如果提交时网络超时，工具不会自动重试。它会在提交前打印已知交易哈希，先用 `receipt` 或区块浏览器查询，避免把“没收到响应”误认为“没提交”。工具不会自动领取测试币或广播。

### 实现与当前验证范围

- 椭圆曲线使用 [libsecp256k1 v0.8.0](https://github.com/bitcoin-core/secp256k1/tree/6e2c8bc4ecdc6e71dbe7a368f360d8d453ce435d)：固定 32 位算术路径，RFC6979 确定性签名、low-S、上下文随机化和签后自验。不开启实验性 ARM 汇编。上下文使用 1 KiB 静态区，无运行期堆分配。
- Keccak-256、HMAC-SHA512、SHA256/RIPEMD160、清零函数取自固定版本的 Trezor crypto。CamWallet 用这些接口实现固定路径 `m/44'/60'/0'/0/0` 的 BIP32 派生；异常子节点直接失败，不悄悄换派生路径。源码版本与许可证见 `CamWallet/third_party/README.md`。
- 板端创建种子/盲化材料采用 Pico SDK 2.3.1 `pico_rand`，该配置混入 RP2350 TRNG；派生后清零种子、中间节点和临时数据。未完成随机源质量、侧信道、掉电残留或生产级密钥保护验证，因此当前固件硬性限定 Sepolia。
- 已创建账户时，请求必须带匹配 sender；可选 source fingerprint 若提供也必须匹配。签名数据直接引用审核时解析的原始 CBOR 内 `sign-data`；签名前重新严格解析并校验。拒绝、触摸错误或未确认不调用签名。
- 签名响应为 `eth-signature` 的两字段 CBOR：tag 37 的 UUID 和 65 字节 `r || s || yParity`；当前 Type 2 的 yParity 为 0/1。总计 89 字节、4 个基础分片。拒绝仍用第 6a 节的 57 字节 `CWREV` 回执。完整消息上限仍为 768 字节。
- 软件验证包含 Keccak 空串向量、账户清除/替换、BIP32 地址跨实现比较、C/Python 确定性签名逐字节比较、UR 签名收发、错误 UUID/发送者/交易/高-S/链号拒绝，以及广播前校验和超时不自动重发。**实物创建、签名耗时、光学回传、真实 RPC 广播及上链仍待按上述步骤验收。**

本机 Release 编译后的 Flash 地址跨度 **238952 B（233.4 KiB）**；静态 RAM（向量表、uninitialized、data、bss）**349296 B（341.1 KiB）**，主核栈仍预留 32768 B。相对触摸审核版增加 Flash 104148 B、静态 RAM 1764 B。计入 quirc 约 92372 B 的堆申请后理论余量约 **49852 B**，未含 malloc 元数据；不等于实测最低可用 RAM。编译器报告 `wallet_sign` 自身栈 624 B，库中较大单函数栈约 2040 B；没有把单函数数字当作完整调用链栈高水位。

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
- **屏幕变成二维码后没有预览**：这是回传状态；普通码保持 30 秒，UR 动态码保持 60 秒。倒计时结束自动恢复。TEST ONLY 模式可按 RESET 提前重启；CREATE 模式不要这样做，RESET 会丢失账户。
- **QR OVER 128 BYTES / QR MODE UNSUPPORTED / QR IS EMPTY**：输入超出当前范围，换成 1–128 字节、支持模式的单帧码；不支持的输入不会截断回传。
- **QR MEMORY ERROR**：识别器分配失败；确认执行了 Configure CMake 并使用本次内存布局设置，再编译烧录。保留日志，不要直接增大帧缓冲或恢复默认栈配置。
- **提示 quirc.h、qrcodegen.h 找不到，或旧版日志中没有 qr=**：确认拿到了新增 `qr/` 和 `third_party/` 文件，执行 Configure CMake，再编译并烧录本次 UF2。
- **编译失败**：保留终端最后一段错误信息，修复后再烧录。

## 8. 日志调试与断点调试

当前流程使用 USB 烧录和 USB 日志，足以进行屏幕、摄像头等模块的初步联调。

VS Code 的 **Debug Project** 断点、单步调试，以及 **Flash Project (SWD)** 烧录，需要外接兼容的 SWD 调试器，例如 Raspberry Pi Debug Probe，并连接板子的 SWD 接口。普通 USB 数据线不提供 SWD 调试能力。

## 9. 后续功能与文档维护

当前已加入 RAM 测试账户创建与 Sepolia 原生币签名，先按第 6b 节完成实物签名和上链验收，再增加 ERC-20 转账审核。账户恢复、助记词和持久存储本轮不做。

每次增加或修改可操作功能时，同步更新本文件的操作步骤、预期结果和常见问题，并区分“代码已加入”和“实物已验证”的状态。

## 10. 可选：在电脑上运行二维码集成测试

该测试使用主机 C 编译器，不使用 Arm 编译器，不连接板子，不替代实物扫码。在仓库根目录运行，环境需要 CMake、Ninja 和主机 GCC/Clang：

```powershell
cmake -S firmware/tests -B firmware/tests/build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build firmware/tests/build
ctest --test-dir firmware/tests/build --output-on-failure
```

直接运行 `firmware/tests/build/qr_roundtrip_test.exe` 可看到 15 项详细结果：普通文本、旋转、镜像、彩色 RGB565 输入、含零字节的二进制、UTF-8/Latin-1 ECI、128 字节边界、超长/空值/不支持 ECI 拒绝，以及输入和渲染边界。测试会将输入渲染成 RGB565 后实际识别，再对固件生成的回传画面重新识别，比较长度、ECI 和全部 payload 字节，并检查帧缓冲边界保护字节。新增损坏二维码测试，确认“已定位但解码失败”会返回明确状态和错误信息，并验证下一帧不会沿用旧诊断。

本次电脑端测试和板端完整交叉编译均已通过；烧录和光学回传由开发者按前文步骤验证。

`ctest` 还包含 `scan_preview` 状态条合成回归测试，检查摄像头画面改变时状态条像素保持一致、中间预览不被改写、长文字裁剪、旧文字清除及缓冲区边界保护。该测试已通过，开发者也已反馈实物闪动修复成功。

新增 `ur_transport` 测试覆盖 768 字节消息、32 片倒序/重复、会话超时、单帧抢占拒绝、内存耗尽后恢复，以及 32 张动态二维码渲染后由 quirc 重新识别。构建后继续运行：

```powershell
.\companion\.venv\Scripts\python.exe -X utf8 .\companion\ur_echo.py --self-test
```

当前 5 组 CTest、60 项 Python 测试全部通过。Python 回归包括原有二维码测试、Foundation Python UR 与 cUR 双向互通、混合帧/丢帧恢复、CRC、消息上限、EVM 严格解析，以及确认/拒绝回执、旧 UUID/摘要/版本/状态拒绝、完整 78 位金额和 64 位链号、真实窗口轮询路径。`firmware/tests/build/ur_bridge.exe` 未构建时会跳过 C 互通测试；要复现全部结果，应先运行上面的 CMake/CTest 命令。软件测试不打开真实摄像头。

新增 `evm_review` C 测试覆盖触摸报告坐标边界、按住/拖动/短触/多点/读取错误、每页拒绝、最后一页确认、没有决定不产生回执，以及全部页面渲染边界。主机可执行 `firmware/tests/build/evm_review_test.exe firmware/tests/build/review` 导出五张 PPM 页面用于检查；已核对极限位数的页面无裁剪。上述验证不能替代实物触摸坐标和光学收发测试。

## 参考资料

- [cUR 源码及传输层 API](https://github.com/odudex/cUR/tree/33d8a6742c37d0d9bd19bdb65269f238d9a024d3)
- [Foundation Python UR 参考实现](https://github.com/Foundation-Devices/foundation-ur-py/tree/a371f6355fb3433cc989ad5bf28f87a347b222fe)
- [ERC-4527 二维码请求格式](https://eips.ethereum.org/EIPS/eip-4527)
- [EIP-1559 Type 2 交易格式](https://eips.ethereum.org/EIPS/eip-1559)
- [Raspberry Pi Pico 官方 VS Code 扩展](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico)
- [Pico C/C++ SDK 入门](https://www.raspberrypi.com/documentation/microcontrollers/c_sdk.html)
- [Debug Probe 与 SWD 调试](https://www.raspberrypi.com/documentation/microcontrollers/debug-probe.html)
- [Microsoft Serial Monitor](https://github.com/microsoft/vscode-serial-monitor)
- [Waveshare 板卡资料](https://www.waveshare.com/wiki/RP2350-Touch-LCD-2)
