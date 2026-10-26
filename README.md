# CamWallet

基于 **RP2350 + OV5640 摄像头** 的 EVM 离线签名器原型。板端使用 C/C++ 固件，电脑端计划使用简单的 Python 工具，通过摄像头和屏幕上的二维码交换交易数据。

**当前已完成 USB 日志和 LCD 显示联调，尚未实现摄像头扫码、密钥管理或交易签名。项目未经安全审计，请勿用于保管真实资产。**

## 硬件与开发环境

- 主板：Waveshare **RP2350-Touch-LCD-2-C**。
- 显示：板载 2 英寸、240×320 电容触摸屏。
- 摄像头：配套 **OV5640 镜头与软排线模块**，直接连接主板的摄像头接口，由 RP2350 驱动。
- 板端开发：**VS Code + 官方 Raspberry Pi Pico 扩展 + Pico SDK**。
- 电脑端开发：**Python**，按功能需要打开二维码显示或摄像头窗口。

摄像头模块没有独立的 ESP32 开发板，当前方案采用 RP2350 单主控。

## 项目组成

- **板端固件**：采集摄像头画面、识别二维码、解析和展示交易、等待用户确认、管理密钥并签名，再通过屏幕输出签名结果二维码。这些是目标功能，当前只接入了 LCD 和 USB 日志。
- **电脑端工具**：查询 EVM RPC、准备未签名交易、显示请求二维码、读取板端签名结果、验证并组装交易，由用户执行广播。目前只有职责说明，尚无 Python 脚本。

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
│   └── CamWallet/                 # 用 VS Code 打开的 Pico SDK 工程
│       ├── CamWallet.c            # 当前屏幕测试程序入口
│       ├── CMakeLists.txt         # 编译配置
│       ├── .vscode/               # 共享的 Pico 插件配置
│       ├── drivers/               # 板级引脚与 LCD 驱动
│       ├── third_party/           # 第三方字体等源码
│       └── THIRD_PARTY_NOTICES.txt # 第三方来源及许可
└── companion/
    └── README.md                  # 电脑端 Python 工具的职责说明
```

## 开始开发

1. 安装 VS Code 和发布者为 Raspberry Pi 的 [Raspberry Pi Pico 扩展](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico)。
2. 用 VS Code 打开 **`firmware/CamWallet/`**，该目录包含 `CMakeLists.txt`；已有工程直接打开或导入即可。
3. 按 [板端操作指南](firmware/README.md) 配置工具链，点击 **Compile Project**。
4. 编译成功后，让板子进入 BOOTSEL 模式，将生成的 `build/CamWallet.uf2` 复制到 **RP2350** 盘。
5. 检查屏幕显示，并通过 USB 串口查看运行日志。

当前测试页包含 `CamWallet`、`LCD READY`、运行秒数和红绿蓝色块。左右镜像修正已写入 LCD 驱动，修改后需要重新编译、烧录并在实物上确认方向。

完整的 BOOT / RESET 按键顺序、Serial Monitor 配置、预期显示和故障排查见 **[firmware/README.md](firmware/README.md)**。

现有工程记录 Pico SDK `2.3.1`、Arm 工具链 `15_2_Rel1`、picotool `2.3.1`，启动板型使用 `pico2`。屏幕使用本板专用引脚；独立板型及完整 16 MB Flash 配置仍待完善。

电脑端规划见 **[companion/README.md](companion/README.md)**，目前尚无可执行工具。

## 开发进度

- [x] 建立板端固件与电脑端工具目录。
- [x] 建立 VS Code Pico SDK 工程。
- [x] 完成 USB 编译、烧录和日志输出的实物验证。
- [x] 加入 LCD 测试页，完成屏幕点亮的实物验证。
- [x] 在源码中修正文字左右镜像，待烧录后核对方向。
- [x] 补充板端操作指南和 Git 忽略规则。
- [ ] 接入触摸输入。
- [ ] 接入 OV5640，采集可供二维码识别的图像。
- [ ] 打通二维码接收与屏幕回传。
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
