# 电脑端 Python 二维码联调工具

`qr_echo.py` 打开一个桌面窗口：先显示请求二维码供板子扫描，再用电脑摄像头读取板端回传，按原始字节自动比较并显示 **PASS / FAIL**。这是光学传输联调工具，尚未实现 EVM 交易准备、解析、签名或广播。

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

`.venv/` 已被仓库的 `.gitignore` 忽略。程序运行时不访问网络、不读取 USB 串口，也不会自动烧录板子。

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
- **终端中文乱码**：使用上述 `-X utf8` 启动命令；数据核对还可以看终端输出的十六进制值。
- **键盘操作**：第 1 步按空格开始读取；读取时按 Esc 取消，其他阶段按 Esc 退出。

## 5. 软件测试和当前验证状态

运行不打开窗口、不访问真实摄像头的软件测试：

```powershell
.\companion\.venv\Scripts\python.exe -X utf8 .\companion\qr_echo.py --self-test
```

已通过 18 项测试，包括真实二维码图像的编码/解码与二进制、中文、128 字节边界、旋转、错误/多码/历史响应、超时、读取前隐藏请求码，以及摄像头成功、失败和取消时的资源释放。摄像头测试使用模拟设备，本机真实摄像头及板端光学 PASS 仍需按第 2 节验收。

退出窗口时，PASS 返回退出码 `0`；FAIL、TIMEOUT 和摄像头读取错误返回 `1`；未完成或取消返回 `130`；缺少依赖、参数错误或窗口初始化失败返回 `2`。

## 后续钱包功能

电脑端后续负责公开地址导入、RPC 查询、EIP-1559 未签名交易准备、签名恢复验证与用户主动广播。助记词和私钥由板端处理。先完成本工具的光学闭环验收，再接入 UR 分帧和钱包请求格式。
