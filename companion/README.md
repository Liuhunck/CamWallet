# 电脑端 Python 二维码联调工具

`qr_echo.py` 测试普通二维码，`ur_echo.py` 测试动态 UR，后者加 `--evm` 可发送固定参数的 EIP-1559 原生币触摸审核测试请求。两者都先显示请求码，再隐藏请求码并用电脑摄像头读取板端回传，完整数据比较后显示 **PASS / FAIL**。这两个工具仍是传输与审核测试。新增 `evm_wallet.py` 负责 Sepolia 参数查询、读取板端签名、独立验签和用户主动广播；私钥只在板端。

所有依赖安装在 **`companion/.venv`**，不会安装到全局 Python。依赖已锁定在 `requirements.txt`，版本范围记录在 `requirements.in`。本机的 Python 3.12 虚拟环境和依赖已经准备好。

## 1. 在当前电脑启动

在 PowerShell 中进入仓库根目录后运行：

```powershell
cd C:\Users\liuhu\Desktop\github\CamWallet
.\companion\.venv\Scripts\python.exe -X utf8 .\companion\qr_echo.py
```

直接使用虚拟环境里的解释器即可，无需运行 `Activate.ps1`，也不需要修改 PowerShell 执行策略。

换电脑或重建环境时，使用带 Tk 桌面支持的 Python 3.12：

```powershell
python -m venv companion/.venv
.\companion\.venv\Scripts\python.exe -m pip install -r companion/requirements.txt
```

`.venv/` 已被仓库的 `.gitignore` 忽略。`qr_echo.py`、`ur_echo.py` 和 `evm_wallet.py sign` 不访问网络。`evm_wallet.py prepare/broadcast/receipt` 使用用户配置的 RPC。工具不读取 USB 串口，也不自动烧录。

## 2. 完成一轮实物验收

1. 板子运行已能扫码的二维码回传固件，处于摄像头预览状态。板端已有回传码时，等其 30 秒倒计时结束，或只按一下 RESET 恢复扫码。
2. 启动工具。窗口显示一张请求码，下方列出本轮发送内容，默认类似 `CamWallet:echo:0123456789abcdef`。每轮使用新的随机值，共 31 字节。
3. 将**板端摄像头**对准电脑窗口中的请求码，保持白边完整、画面清晰。等板端切换到 `QR ECHO / TEST` 回传二维码画面。
4. 立即点击电脑窗口的 **“板子已显示回传码，开始读取”**，或按空格。工具先隐藏请求码，再打开电脑摄像头，切换为实时预览。
5. 将**板端屏幕**对准电脑摄像头，让回传码和完整白边进入画面。读取阶段默认最多等待 30 秒，窗口显示剩余时间；板端的 30 秒保持时间从它显示回传码时开始，因此第 4 步要及时操作。
6. 结果显示在窗口和启动它的终端：
   - **PASS**：读取到的唯一二维码与本轮发送内容逐字节相等。
   - **FAIL**：读到了不同内容，或画面中同时出现多个二维码；窗口会显示实际收到的内容。
   - **TIMEOUT**：在限时内未读到有效二维码；这与内容不一致分开报告。
   - 摄像头错误：明确显示打开或采集失败原因。
7. 点击 **“新一轮”**重新测试，等待板子恢复扫码或按 RESET。每次重新生成随机测试内容，可以区分上一次残留的回传码。退出窗口会释放摄像头。

**只有第 6 步出现 PASS，才算本轮“电脑发码 → 板子扫码回传 → 电脑摄像头读回”的光学闭环通过。** 软件自测不能替代这一步。

读取前隐藏请求码，可以避免摄像头直接扫到电脑原码而误报成功。读取期间调整窗口大小也不会重新显示请求码；请保持画面中只有板端回传码。

## 3. 摄像头与测试内容参数

默认摄像头编号为 `0`。窗口底部可在读取前改为 `1`、`2` 等，也可从命令行设置：

```powershell
.\companion\.venv\Scripts\python.exe -X utf8 .\companion\qr_echo.py --camera 1
```

Windows 默认后端无法打开摄像头或黑屏时，可尝试 DirectShow 或 Media Foundation：

```powershell
.\companion\.venv\Scripts\python.exe -X utf8 .\companion\qr_echo.py --camera 0 --backend dshow
.\companion\.venv\Scripts\python.exe -X utf8 .\companion\qr_echo.py --camera 0 --backend msmf
```

复现旧测试码，或测试其他 UTF-8 文本：

```powershell
.\companion\.venv\Scripts\python.exe -X utf8 .\companion\qr_echo.py --text 'CamWallet:hello-12345'
.\companion\.venv\Scripts\python.exe -X utf8 .\companion\qr_echo.py --text '你好，CamWallet' --timeout 45
```

固定 `--text` 时，每轮内容相同，不能据此区分历史回传。默认随机测试适合正式验收。

当前内容限制为 **1–128 个 UTF-8 字节**，按字节而非字符计数；工具会在发码前检查，空字符串和超长内容会报错。接收端直接比较二维码原始字节，不依赖扫码库对文本编码的猜测。

## 4. 常见问题

- **打不开摄像头**：先关闭系统相机、视频会议或其他占用相机的软件；检查 Windows“隐私和安全性 → 相机 → 允许桌面应用访问相机”；再尝试不同编号和 `--backend dshow`。
- **有电脑预览但没有读到码**：先确认镜头前是板端屏幕；保持四周白边完整，改变距离和角度，等摄像头对焦。识别使用原始摄像头画面，预览缩放不会降低送去解码的分辨率。
- **FAIL 显示上一次的内容**：板子还处于上轮 30 秒保持状态。等待其恢复扫码或按 RESET，再点击“新一轮”。
- **新一轮暂时不可点击**：上一轮摄像头线程正在结束并释放设备，稍等片刻。读取期间可以先点“取消读取”。
- **没有桌面窗口 / Tcl 错误**：从正常桌面终端运行虚拟环境的 Python；基础 Python 需要带 Tk 支持。Codex 执行沙箱可能阻止 Tcl 读取初始化文件，本机已在沙箱外检查 Tcl 8.6.12 可用。
- **点击动态 UR“开始读取”后反复报 `TypeError: object of type 'NoneType' has no len()`**：这是首个分片到达前查询进度的电脑端缺陷，现已修复。关闭旧工具窗口，使用同一 `.venv` 命令重新启动，无需重烧板端固件。未读到有效分片时应显示“等待首个有效UR分片”，随后显示 `1/N` 等实际进度。
- **终端中文乱码**：使用上述 `-X utf8` 启动命令；数据核对还可以看终端输出的十六进制值。
- **键盘操作**：第 1 步按空格开始读取；读取时按 Esc 取消，其他阶段按 Esc 退出。

## 5. 软件测试和当前验证状态

运行不打开窗口、不访问真实摄像头的软件测试：

```powershell
.\companion\.venv\Scripts\python.exe -X utf8 .\companion\qr_echo.py --self-test
```

原有 18 项测试包括真实二维码图像编码/解码、二进制、中文、128 字节边界、旋转、错误/多码/历史响应、超时、读取前隐藏请求码，以及摄像头资源释放。加上 UR/EVM 跨语言互通、确认/拒绝回执和窗口轮询回归，加上 Sepolia 签名与广播校验后共 60 项通过；摄像头测试使用模拟设备，新增审核回执的光学流程仍待验收。

退出窗口时，PASS 返回退出码 `0`；FAIL、TIMEOUT 和摄像头读取错误返回 `1`；未完成或取消返回 `130`；缺少依赖、参数错误或窗口初始化失败返回 `2`。

## 6. 动态 UR 与 EVM 触摸审核测试

先按 [firmware/README.md 第 6a 节](../firmware/README.md#6a-cur-动态二维码与-evm-触摸审核) 编译并烧录固件，开机选择 TEST ONLY，然后运行：

```powershell
# 默认256字节随机数据，11个基础分片，400ms一帧
.\companion\.venv\Scripts\python.exe -X utf8 .\companion\ur_echo.py

# EVM测试：Sepolia固定转账参数，UUID每轮随机；不签名
.\companion\.venv\Scripts\python.exe -X utf8 .\companion\ur_echo.py --evm
```

普通模式收齐后显示 `UR ECHO / TEST`。EVM 模式收齐后进入 `REVIEW 1/5`，在板端用 NEXT/BACK 审核网络、完整地址、金额和费用；最后一页点 CONFIRM，或任意页点 REJECT，松手后才显示回传二维码。审核期间不会自动确认或回传。看到 `CONFIRMED / NO SIGN` 或 `REJECTED / NO SIGN` 后，再点击电脑“开始读取”，把板端屏幕对准电脑摄像头。回传保持 60 秒；收齐且 UUID、请求摘要一致才显示 PASS，并明确标出 CONFIRMED 或 REJECTED。拒绝结果的 PASS 表示本轮回执核验成功。旧固件的原样 EVM 回传会 FAIL，需要重新烧录并重启电脑工具。

可选参数：`--camera 1`、`--backend dshow`、`--interval 600`（电脑发码间隔）、`--timeout 60`（电脑读回期限）、`--bytes 765`（普通 UR 数据上限）。板端从首个有效片起只有 30 秒收集时间，大包发码太慢会超时；换请求前等待板端重置或按 RESET。

电脑端使用固定版本的 Foundation Python UR 参考实现，板端使用 cUR，源码与许可均已随项目提供，UR 传输库本身无需额外 pip 包；新增签名工具和完整软件测试需先按第 7 节同步 `.venv` 的锁定依赖。

完整软件验证顺序：

```powershell
cmake -S firmware/tests -B firmware/tests/build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build firmware/tests/build
ctest --test-dir firmware/tests/build --output-on-failure
.\companion\.venv\Scripts\python.exe -X utf8 .\companion\ur_echo.py --self-test
```

C 测试需要主机 GCC/Clang，不使用 Arm GCC。Python 的独立互通测试调用生成的 `ur_bridge.exe`；该文件缺失时此类测试会标记 SKIP，其余测试照常运行。已验证双向分帧一致、乱序/重复/混合帧/丢帧恢复、资源限制、CRC 拒绝和受限 EVM 解析；新增 Tk 窗口已通过隐藏窗口初始化检查，没有自动打开真实摄像头。

EVM 模式默认发送 chain ID 11155111、nonce 0、地址 `0x1111111111111111111111111111111111111111`、金额 `10^15 wei`、gas 21000、max fee 2 gwei、priority fee 1 gwei。回传为 `ur:bytes` 包装的 57 字节审核回执，包含版本、确认/拒绝、请求 UUID 和完整请求 CBOR 的 SHA-256，**不是 `eth-signature`**。摘要用于绑定请求，没有设备身份认证，不表示签名或广播成功。协议和两种结果的完整验收步骤见 firmware 第 6a 节。

## 7. Sepolia 签名与广播

完整步骤、预期日志和限制见 [firmware 第 6b 节](../firmware/README.md#6b-创建账户并在-sepolia-签名)。板端开机选 CREATE 创建账户，记录屏幕二维码或串口中的公开地址，准备少量 Sepolia ETH，并在整个测试期间保持供电。RESET 会丢失账户，本轮没有恢复。

```powershell
uv pip sync companion/requirements.txt --python companion/.venv/Scripts/python.exe
$env:CAMWALLET_RPC_URL = 'https://你的Sepolia-RPC地址'
$sender = '0x板端生成的完整地址'
.\companion\.venv\Scripts\python.exe -X utf8 .\companion\evm_wallet.py prepare --sender $sender --to $sender --amount-eth 0.000001 --out companion/local/request1.json
.\companion\.venv\Scripts\python.exe -X utf8 .\companion\evm_wallet.py sign companion/local/request1.json --out companion/local/signed1.json
# 只有下面这一步主动广播；先完成板端 CONFIRM 和电脑验签。
.\companion\.venv\Scripts\python.exe -X utf8 .\companion\evm_wallet.py broadcast companion/local/signed1.json
.\companion\.venv\Scripts\python.exe -X utf8 .\companion\evm_wallet.py receipt 0x完整交易哈希
```

签名窗口读到 SIGNED 回传后才保存交易；拒绝、旧回执、错误签名不保存。输出文件不覆盖，下一轮使用 request2/signed2 等新路径。文件只含公开交易数据，位于 Git 忽略的 `companion/local/`。主网、ERC-20、合约收款地址暂不支持。广播超时时先查打印的交易哈希，不自动重发。

## 后续钱包功能

先完成第 7 节的实物和上链验收，再加入 ERC-20 转账审核、更方便的地址导入和交互。账户恢复本轮不做。
