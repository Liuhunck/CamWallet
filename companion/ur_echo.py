"""Animated UR optical test; run with companion/.venv's Python."""
from __future__ import annotations
import argparse
import math
import sys
import time
from pathlib import Path

from qr_echo import EchoWindow, CameraWorker, Phase, make_request_image
from ur_protocol import URSession, encoder, new_ur_request


class URWindow(EchoWindow):
    def __init__(self, root, args):
        self.animation = None
        super().__init__(root, args)
        root.title("CamWallet 动态UR / EVM触摸审核测试")

    def make_session(self):
        ur_type, payload, expected = new_ur_request(self.args.evm, self.args.bytes)
        return ur_type, payload, URSession(expected, self.args.timeout, review=self.args.evm)

    def new_round(self):
        if self.worker is not None and self.worker.is_alive():
            return
        if self.animation is not None:
            self.root.after_cancel(self.animation)
            self.animation = None
        self.worker = None
        ur_type, payload, self.session = self.make_session()
        expected = self.session.expected
        self.encoder = encoder(ur_type, payload)
        self.last_frame = None
        self.actual_text.set("尚未收集回传码")
        identity = f"；UUID={expected[:16].hex()}" if self.args.evm else ""
        self.expected_text.set(f"{ur_type}: {len(payload)} CBOR字节{identity}；仅测试、不签名")
        self.instructions.configure(text=(
            "让板子收齐动态码，在板端逐页审核后点 CONFIRM 或 REJECT；出现回传二维码后再点击“开始读取”。"
            if self.args.evm else "让板子收齐动态码；等板端出现 UR ECHO，再点击“开始读取”。"))
        print(f"REQUEST type={ur_type} cbor={payload.hex()}", flush=True)
        self.animate()

    def animate(self):
        self.animation = None
        if self.closed or self.session.phase != Phase.REQUEST:
            return
        part = self.encoder.next_part().upper().encode("ascii")
        self.request_image = make_request_image(part)
        seq = self.encoder.fountain_encoder.seq_num
        self.status.configure(text=f"第1步：扫描动态UR，第{seq}帧（基础分片{self.encoder.fountain_encoder.seq_len()}个）")
        self.redraw()
        self.animation = self.root.after(self.args.interval, self.animate)

    def start_receiving(self):
        if self.session.phase != Phase.REQUEST or (self.worker is not None and self.worker.is_alive()):
            return
        try:
            index = int(self.camera_index.get())
            if index < 0:
                raise ValueError
        except ValueError:
            self.status.configure(text="摄像头编号请填写0或更大的整数。")
            return
        self.session.begin(time.monotonic())
        self.instructions.configure(text="请求码已隐藏。把板端屏幕对准电脑摄像头，保持到完整消息校验完成。")
        self.redraw()
        self.root.update_idletasks()
        self.worker = CameraWorker(index, self.args.backend, continuous=True)
        self.worker.start()

    def poll(self):
        super().poll()
        if not self.closed and self.session.phase == Phase.RECEIVING:
            recovered, expected = self.session.part_counts()
            remaining = max(0, math.ceil(self.session.deadline - time.monotonic()))
            progress = (f"回传已恢复 {recovered}/{expected} 个分片" if expected
                        else "等待首个有效UR分片")
            self.status.configure(text=f"第2步：{progress}，还剩{remaining}秒")

    def finish(self):
        super().finish()
        self.instructions.configure(text="本轮结束。板端回传保持60秒；等待恢复扫码或按RESET，再点击“新一轮”。PASS不代表交易签名成功。")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evm", action="store_true", help="发送Sepolia原生币测试请求，触摸审核并回传确认/拒绝，不签名")
    parser.add_argument("--bytes", type=int, default=256, help="普通UR随机数据长度16–765，默认256")
    parser.add_argument("--interval", type=int, default=400, help="发码间隔毫秒，200–2000，默认400")
    parser.add_argument("--camera", type=int, default=0)
    parser.add_argument("--backend", choices=("auto", "dshow", "msmf"), default="auto")
    parser.add_argument("--timeout", type=float, default=60)
    parser.add_argument("--self-test", action="store_true", help="软件测试，不打开窗口或摄像头")
    args = parser.parse_args(argv)
    if args.camera < 0 or not 200 <= args.interval <= 2000:
        parser.error("摄像头编号需非负，间隔需为200–2000ms。")
    try:
        _, _, expected = new_ur_request(args.evm, args.bytes)
        URSession(expected, args.timeout, review=args.evm)
    except ValueError as error:
        parser.error(str(error))
    if args.self_test:
        import unittest
        tests = unittest.defaultTestLoader.discover(str(Path(__file__).parent / "tests"))
        return 0 if unittest.TextTestRunner(verbosity=2).run(tests).wasSuccessful() else 1
    try:
        import tkinter as tk
    except ImportError as error:
        print(f"缺少Tk支持：{error}", file=sys.stderr)
        return 2
    try:
        root = tk.Tk()
        app = URWindow(root, args)
        root.mainloop()
    except (ImportError, tk.TclError) as error:
        print(f"无法打开工具：{error}。请使用companion/.venv及带Tk的桌面Python。", file=sys.stderr)
        return 2
    return 0 if app.session.phase == Phase.PASSED else 1


if __name__ == "__main__":
    raise SystemExit(main())
