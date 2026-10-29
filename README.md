# CamWallet

基于 **RP2350 + OV5640 摄像头** 的 EVM 离线签名器原型。板端使用 C/C++ 固件，电脑端使用 Python 工具，通过摄像头和屏幕上的二维码交换数据。

**USB 日志、LCD、摄像头预览、画面方向和二维码扫描已收到实物成功反馈。板端使用 quirc 单帧扫码和 qrcodegen 原样回传，最多 128 字节，显示回传码 30 秒后自动恢复扫码。板端二维码模块通过 15 项电脑测试；新增 Python 配套工具通过 18 项软件测试，虚拟环境及依赖已准备好，真实电脑摄像头自动读回 PASS 仍待验收。尚未实现密钥管理或交易签名。项目未经安全审计，请勿用于保管真实资产。**

## 硬件与开发环境

- 主板：Waveshare **RP2350-Touch-LCD-2-C**。
- 显示：板载 2 英寸、240×320 电容触摸屏。
- 摄像头：配套 **OV5640 镜头与软排线模块**，直接连接主板的摄像头接口，由 RP2350 驱动。
- 板端开发：**VS Code + 官方 Raspberry Pi Pico 扩展 + Pico SDK**。
- 电脑端开发：**Python**，按功能需要打开二维码显示或摄像头窗口。

摄像头模块没有独立的 ESP32 开发板，当前方案采用 RP2350 单主控。

## 项目组成

- **板端固件**：采集摄像头画面、识别二维码、解析和展示交易、等待用户确认、管理密钥并签名，再通过屏幕输出签名结果二维码。当前入口是摄像头扫码与原样回传测试，开发者已反馈扫码成功，交易与签名功能仍待开发。
- **电脑端工具**：当前 `qr_echo.py` 显示随机请求二维码、隐藏请求码后读取电脑摄像头，并逐字节比较回传内容，显示 PASS / FAIL。RPC、交易准备、签名验证与广播后续接入。

目标使用流程：

1. 板端创建或恢复钱包，通过二维码导出公开地址。
2. 电脑端获取 nonce 和费用参数，准备交易并显示二维码。
3. 板端扫码，独立解析交易，在自己的屏幕上展示待确认内容。
4. 用户核对网络、完整收款地址、金额和手续费上限后确认签名。
5. 板端显示签名结果二维码，电脑摄像头读取。
6. 电脑端验证签名与发送地址，组装交易并广播。

助记词和私钥计划仅由板端处理。第一版计划采用使用期间保存在 RAM 的无状态设计，尚未实现。

## 目录结构

```text
CamWallet/
├── firmware/
│   ├── README.md                  # 板端环境配置、编译、烧录和调试指南
│   ├── official-demo/             # 官方摄像头 UF2 对照说明，UF2 本身由 Git 忽略
│   ├── tests/                     # 二维码电脑端集成测试与 camwallet-hello.svg 测试码
│   └── CamWallet/                 # 用 VS Code 打开的 Pico SDK 工程
│       ├── CamWallet.c            # 摄像头扫码、屏幕回传和自动恢复入口
│       ├── CMakeLists.txt         # 编译配置
│       ├── .vscode/               # 共享的 Pico 插件配置
│       ├── drivers/               # 板级引脚、LCD、OV5640 与 PIO 采集程序
│       ├── qr/                    # 灰度识别、限长、编码与回传码渲染
│       ├── ui/                    # 扫码预览状态条合成，避免分步刷屏闪动
│       ├── third_party/           # 固定版本 quirc、qrcodegen、官方字体和寄存器表
│       └── THIRD_PARTY_NOTICES.txt # 第三方来源及许可
└── companion/
    ├── README.md                  # 虚拟环境、启动、摄像头设置和自动验收步骤
    ├── qr_echo.py                 # 二维码显示、摄像头读回和 PASS / FAIL
    ├── requirements.in            # 直接依赖版本范围
    ├── requirements.txt           # 固定依赖版本
    └── tests/                     # 18 项软件测试
```

## 开始开发

1. 安装 VS Code 和发布者为 Raspberry Pi 的 [Raspberry Pi Pico 扩展](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico)。
2. 用 VS Code 打开 **`firmware/CamWallet/`**，该目录包含 `CMakeLists.txt`；已有工程直接打开或导入即可。
3. 按 [板端操作指南](firmware/README.md) 配置工具链，点击 **Compile Project**。
4. 编译成功后，让板子进入 BOOTSEL 模式，将生成的 `build/CamWallet.uf2` 复制到 **RP2350** 盘。
5. 检查屏幕显示，并通过 USB 串口查看运行日志。

当前固件启动后显示 `STARTING CAMERA`，初始化成功后进入 240×320 彩色预览，顶部显示 `NO QR FOUND` 或具体解码状态，底部显示候选数与解码数。用摄像头扫描 [测试二维码](firmware/tests/camwallet-hello.svg)，成功后应切换为 `QR ECHO / TEST` 回传画面；读回内容应完整等于 **`CamWallet:hello-12345`**，串口同时输出 `text` 和 `payload_hex`。回传码保持 30 秒，再自动恢复扫码；只按 RESET 可提前重启扫码。操作步骤与支持范围见 [板端指南第 6 节](firmware/README.md#6-二维码扫描与屏幕回传操作)。

双缓冲让下一帧采集与当前帧识别、刷屏重叠，LCD SPI 请求为 75 MHz，已确认正常的屏幕和摄像头方向保留。USB 日志显示 `qr` 识别耗时；`qr` 有数值只说明执行了识别。DIAG2 版每秒输出 `[QR] scan` 候选数量、灰度范围和解码错误；成功结果在 RAM 中保留并约每 5 秒重复输出。新增库随仓库提供，无需编译时下载。当前扫码版实物反馈约 8.1 FPS、`qr=29ms`，光学回传仍待验收，摄像头基线参考 [官方 demo 对照](firmware/official-demo/README.md)。

完整的 BOOT / RESET 按键顺序、Serial Monitor 配置、预期显示和故障排查见 **[firmware/README.md](firmware/README.md)**。

现有工程记录 Pico SDK `2.3.1`、Arm 工具链 `15_2_Rel1`、picotool `2.3.1`，启动板型使用 `pico2`。屏幕使用本板专用引脚；独立板型及完整 16 MB Flash 配置仍待完善。

电脑端自动验收工具已提供，使用项目内的虚拟环境启动，不需要激活环境：

```powershell
.\companion\.venv\Scripts\python.exe -X utf8 .\companion\qr_echo.py
```

窗口先显示本轮请求码，板子扫码显示回传码后，点击“开始读取”，将板端屏幕对准电脑摄像头。工具会自动比较并显示 PASS / FAIL。安装和完整步骤见 **[companion/README.md](companion/README.md)**。

## 开发进度

- [x] 建立板端固件与电脑端工具目录。
- [x] 建立 VS Code Pico SDK 工程。
- [x] 完成 USB 编译、烧录和日志输出的实物验证。
- [x] 加入 LCD 测试页，完成屏幕点亮的实物验证。
- [x] 修正文字左右镜像，实物已确认显示正常。
- [x] 补充板端操作指南和 Git 忽略规则。
- [ ] 接入触摸输入。
- [x] 加入 OV5640 初始化、PIO/DMA 单帧采集与 LCD 实时预览代码。
- [x] 完成摄像头预览出图的实物验证。
- [x] 烧录官方摄像头 demo 对照，实物反馈更流畅。
- [x] 加入双缓冲流水采集、32 位 DMA、75 MHz SPI 请求和 VSYNC 帧周期日志；扫码循环实物约 8.1 FPS。
- [x] 摄像头垂直翻转修正，实物已确认方向正常。
- [x] 收到扫码版 8.1 FPS 实物日志，延迟及长期稳定性继续验证。
- [x] 接入 quirc 灰度识别和 qrcodegen 单帧原样回传，完成电脑端 15 项集成测试；增加候选数量、解码错误和历史结果日志。
- [x] 开发者反馈板端二维码扫描成功，扫码循环实物约 8.1 FPS。
- [x] 将预览画面与上下状态条合成后一次刷屏，开发者已确认闪动修复成功。
- [x] 添加 Python 二维码联调工具、项目内 `.venv` 和固定依赖，完成 18 项软件测试。
- [ ] 使用真实电脑摄像头完成自动读回 PASS 验收。
- [ ] 增加 UR 动态二维码分帧重组及钱包请求格式。
- [ ] 实现 EVM 交易解析、展示和确认。
- [ ] 实现密钥管理与签名。
- [ ] 实现电脑端 Python 工具，在本地 EVM 测试链完成签名闭环。

## 第一版钱包的目标范围

- 单个 EVM 账户，固定派生路径 `m/44'/60'/0'/0/0`。
- 支持 EIP-1559 Type 2 原生币转账，第一版仅接受空 calldata、空 access list 的交易。
- 板端根据实际待签数据展示 Chain ID、收款地址、金额、nonce 和手续费上限。
- 二维码传输计划参考 UR 编码及 ERC-4527 的 `eth-sign-request` / `eth-signature` 格式。
- 合约授权、EIP-712、消息签名和其他交易类型分阶段增加。

以上为开发目标，尚未实现。最终签名逻辑必须保证展示内容与实际签署内容一致，解析失败或不支持的请求应拒绝签名。

## 开发约定

- 编译、烧录由开发者在 VS Code 和文件资源管理器中手动执行，当前使用 USB 日志联调。
- 每次新增功能同步更新对应目录的 README，写明操作步骤、预期结果和实物验证状态。
- 共享的 `.vscode` 编译、调试及工具链配置进入版本管理。
- `build/`、UF2 等编译产物、Python 虚拟环境、缓存和本地 `.env` 文件由 `.gitignore` 排除。
- USB 当前用于开发烧录和日志；钱包运行阶段的隔离策略、启动保护和密钥处理仍需实现和验证。二维码输入也需要严格解析和资源限制。

## 许可

项目使用 [Apache License 2.0](LICENSE)。第三方字体及改编驱动的来源和许可见 [THIRD_PARTY_NOTICES.txt](firmware/CamWallet/THIRD_PARTY_NOTICES.txt)。
