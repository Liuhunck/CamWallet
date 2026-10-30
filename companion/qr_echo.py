"""Two-stage camera QR echo test for the CamWallet prototype."""

from __future__ import annotations

import argparse
import math
import queue
import secrets
import sys
import threading
import time
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path


MAX_PAYLOAD_BYTES = 128


class Phase(str, Enum):
    REQUEST = "REQUEST"
    RECEIVING = "RECEIVING"
    PASSED = "PASS"
    FAILED = "FAIL"
    TIMEOUT = "TIMEOUT"
    CANCELLED = "CANCELLED"
    ERROR = "ERROR"


def new_payload() -> bytes:
    return f"CamWallet:echo:{secrets.token_hex(8)}".encode("ascii")


def preview(data: bytes) -> str:
    value = repr(data[:MAX_PAYLOAD_BYTES].decode("utf-8", errors="backslashreplace"))
    return value + (" …（仅显示前128字节）" if len(data) > MAX_PAYLOAD_BYTES else "")


@dataclass
class EchoSession:
    expected: bytes
    timeout: float = 30.0
    phase: Phase = field(default=Phase.REQUEST, init=False)
    received: tuple[bytes, ...] = field(default=(), init=False)
    detail: str = field(default="", init=False)
    deadline: float | None = field(default=None, init=False)

    def __post_init__(self) -> None:
        if not isinstance(self.expected, bytes) or not 1 <= len(self.expected) <= MAX_PAYLOAD_BYTES:
            raise ValueError("测试内容必须为1–128字节；中文按UTF-8字节数计算。")
        if not math.isfinite(self.timeout) or not 0 < self.timeout <= 300:
            raise ValueError("读取超时必须大于0且不超过300秒。")

    def begin(self, now: float) -> None:
        if self.phase != Phase.REQUEST:
            raise ValueError("请先开始新一轮测试。")
        self.deadline = now + self.timeout
        self.phase = Phase.RECEIVING

    def expire(self, now: float) -> bool:
        if self.phase == Phase.RECEIVING and self.deadline is not None and now >= self.deadline:
            self.phase = Phase.TIMEOUT
            self.detail = "超时：未能读取回传二维码。请检查对焦、距离，或开始新一轮。"
            return True
        return False

    def accept(self, payloads: list[bytes], now: float) -> bool:
        self.expire(now)
        if self.phase != Phase.RECEIVING or not payloads:
            return False
        self.received = tuple(payloads)
        if len(payloads) != 1:
            self.phase = Phase.FAILED
            self.detail = "FAIL：画面中有多个二维码，请只保留板端回传码。"
        elif payloads[0] == self.expected:
            self.phase = Phase.PASSED
            self.detail = f"PASS：收到的{len(self.expected)}字节与本轮发送内容完全一致。"
        else:
            self.phase = Phase.FAILED
            self.detail = f"FAIL：内容不一致（发送{len(self.expected)}字节，收到{len(payloads[0])}字节）。"
        return True


def make_request_image(payload: bytes):
    import qrcode

    EchoSession(payload)  # Enforce the board's byte limit before rendering.
    code = qrcode.QRCode(error_correction=qrcode.constants.ERROR_CORRECT_M, box_size=1, border=4)
    code.add_data(payload, optimize=0)
    code.make(fit=True)
    return code.make_image(fill_color="black", back_color="white").convert("RGB")


def decode_qrs(image) -> list[bytes]:
    import zxingcpp

    results = zxingcpp.read_barcodes(
        image, formats=zxingcpp.BarcodeFormat.QRCode,
        try_rotate=True, try_downscale=True, try_invert=True,
    )
    return [bytes(result.bytes) for result in results if result.valid]


class CameraWorker(threading.Thread):
    """Own the camera on this thread; Tk receives bounded, latest-frame updates."""

    def __init__(self, index: int, backend: str, *, continuous: bool = False) -> None:
        super().__init__(daemon=True)
        self.index = index
        self.backend = backend
        self.continuous = continuous
        self.stop_event = threading.Event()
        self.frames: queue.Queue = queue.Queue(maxsize=1)
        self.events: queue.SimpleQueue = queue.SimpleQueue()

    def stop(self) -> None:
        self.stop_event.set()

    def run(self) -> None:
        import cv2

        if self.stop_event.is_set():
            self.events.put(("stopped", None))
            return
        capture = None
        try:
            backends = {"auto": cv2.CAP_ANY, "dshow": cv2.CAP_DSHOW, "msmf": cv2.CAP_MSMF}
            capture = cv2.VideoCapture(self.index, backends[self.backend])
            if not capture.isOpened():
                raise RuntimeError(f"无法打开摄像头{self.index}。检查摄像头编号、占用和Windows相机权限。")
            capture.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
            capture.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)
            self.events.put(("opened", None))
            bad_since = None
            next_decode = 0.0
            while not self.stop_event.is_set():
                ok, frame = capture.read()
                if not ok or frame is None or frame.size == 0:
                    bad_since = bad_since if bad_since is not None else time.monotonic()
                    if time.monotonic() - bad_since >= 3:
                        raise RuntimeError("摄像头连续3秒没有返回画面，请检查连接和占用。")
                    self.stop_event.wait(0.05)
                    continue
                bad_since = None
                try:
                    self.frames.get_nowait()
                except queue.Empty:
                    pass
                self.frames.put_nowait(frame)
                if time.monotonic() >= next_decode:
                    payloads = decode_qrs(frame)
                    next_decode = time.monotonic() + 0.1
                    if payloads:
                        if not self.stop_event.is_set():
                            self.events.put(("codes", payloads))
                        if not self.continuous:
                            break
                self.stop_event.wait(0.01)
        except Exception as error:
            if not self.stop_event.is_set():
                self.events.put(("error", str(error)))
        finally:
            if capture is not None:
                capture.release()
            self.events.put(("stopped", None))


class EchoWindow:
    def __init__(self, root, args: argparse.Namespace) -> None:
        import tkinter as tk
        from tkinter import ttk

        self.root = root
        self.args = args
        self.worker: CameraWorker | None = None
        self.session: EchoSession
        self.last_frame = None
        self.request_image = None
        self.photo = None
        self.closed = False
        root.title("CamWallet 二维码回传测试")
        width = min(900, root.winfo_screenwidth() - 60)
        height = min(820, root.winfo_screenheight() - 80)
        root.geometry(f"{width}x{height}")
        root.minsize(600, 600)
        root.protocol("WM_DELETE_WINDOW", self.close)
        body = ttk.Frame(root, padding=12)
        body.pack(fill="both", expand=True)
        ttk.Label(body, text="CamWallet 二维码回传测试", font=("Microsoft YaHei UI", 16)).pack(anchor="w")
        self.status = ttk.Label(body, wraplength=800, font=("Microsoft YaHei UI", 12))
        self.status.pack(anchor="w", pady=(10, 4))
        self.instructions = ttk.Label(body, wraplength=800)
        self.instructions.pack(anchor="w", pady=(0, 8))
        self.canvas = tk.Canvas(body, background="white", highlightthickness=1, height=460)
        self.canvas.pack(fill="both", expand=True)
        self.canvas.bind("<Configure>", lambda _event: self.redraw())
        self.expected_text = tk.StringVar()
        self.actual_text = tk.StringVar(value="尚未读取回传码")
        ttk.Label(body, text="本轮发送内容：").pack(anchor="w", pady=(8, 0))
        ttk.Entry(body, textvariable=self.expected_text, state="readonly").pack(fill="x")
        ttk.Label(body, textvariable=self.actual_text, wraplength=800).pack(anchor="w", pady=6)
        controls = ttk.Frame(body)
        controls.pack(fill="x")
        ttk.Label(controls, text="摄像头编号：").pack(side="left")
        self.camera_index = tk.StringVar(value=str(args.camera))
        self.camera_input = ttk.Spinbox(controls, from_=0, to=9, width=4, textvariable=self.camera_index)
        self.camera_input.pack(side="left", padx=(0, 8))
        self.start_button = ttk.Button(controls, text="板子已显示回传码，开始读取", command=self.start_receiving)
        self.start_button.pack(side="left", padx=4)
        self.cancel_button = ttk.Button(controls, text="取消读取", command=self.cancel)
        self.cancel_button.pack(side="left", padx=4)
        self.new_button = ttk.Button(controls, text="新一轮", command=self.new_round)
        self.new_button.pack(side="right")
        root.bind("<space>", lambda _event: self.start_receiving() if self.session.phase == Phase.REQUEST else None)
        root.bind("<Escape>", lambda _event: self.cancel() if self.session.phase == Phase.RECEIVING else self.close())
        self.new_round()
        self.root.after(50, self.poll)

    def new_round(self) -> None:
        if self.worker is not None and self.worker.is_alive():
            return
        self.worker = None
        expected = self.args.text.encode("utf-8") if self.args.text is not None else new_payload()
        self.session = EchoSession(expected, self.args.timeout)
        self.request_image = make_request_image(expected)
        self.last_frame = None
        self.expected_text.set(preview(expected))
        self.actual_text.set("尚未读取回传码")
        self.status.configure(text="第1步：让板端摄像头扫描下面的请求码")
        self.instructions.configure(text="等板子显示 QR ECHO / TEST 后，立即点击“开始读取”或按空格，再把板端屏幕对准电脑摄像头。")
        print(f"REQUEST bytes={len(expected)} text={preview(expected)}", flush=True)
        self.redraw()

    def start_receiving(self) -> None:
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
        self.status.configure(text="第2步：读取板端屏幕上的回传码")
        self.instructions.configure(text="请求码已隐藏。请只让板端回传码进入电脑摄像头画面；完整白边和定位方块都要可见。")
        self.redraw()
        # Paint away the outgoing QR before opening the camera. Otherwise the
        # webcam could read our own request and falsely report an optical PASS.
        self.root.update_idletasks()
        self.worker = CameraWorker(index, self.args.backend)
        self.worker.start()

    def cancel(self) -> None:
        if self.session.phase == Phase.RECEIVING:
            self.session.phase = Phase.CANCELLED
            self.session.detail = "已取消读取。点击“新一轮”生成新的请求码。"
            self.finish()

    def finish(self) -> None:
        if self.worker is not None:
            self.worker.stop()
        self.status.configure(text=self.session.detail)
        self.instructions.configure(text="本轮结束。点击“新一轮”重测；板子保持回传码30秒，等待其恢复扫码或按一下RESET。")
        if self.session.received:
            self.actual_text.set("收到：" + " | ".join(preview(value) for value in self.session.received))
        else:
            self.actual_text.set("本轮没有读到有效二维码。")
        print(f"{self.session.phase.value} {self.session.detail}", flush=True)
        print(f"expected_hex={self.session.expected.hex()}", flush=True)
        for i, value in enumerate(self.session.received):
            print(f"received[{i}] bytes={len(value)} hex={value[:MAX_PAYLOAD_BYTES].hex()}", flush=True)

    def redraw(self) -> None:
        from PIL import Image, ImageTk

        if self.closed:
            return
        width, height = max(1, self.canvas.winfo_width()), max(1, self.canvas.winfo_height())
        self.canvas.delete("all")
        if self.session.phase == Phase.REQUEST and self.request_image is not None:
            scale = max(1, min(width - 8, height - 8) // self.request_image.width)
            image = self.request_image.resize((self.request_image.width * scale,) * 2, Image.Resampling.NEAREST)
        elif self.last_frame is not None:
            import cv2

            image = Image.fromarray(cv2.cvtColor(self.last_frame, cv2.COLOR_BGR2RGB))
            image.thumbnail((width, height), Image.Resampling.LANCZOS)
        else:
            self.photo = None
            text = "正在打开电脑摄像头…" if self.session.phase == Phase.RECEIVING else "请求二维码已隐藏"
            self.canvas.create_text(width // 2, height // 2, text=text, font=("Microsoft YaHei UI", 14))
            return
        self.photo = ImageTk.PhotoImage(image)
        self.canvas.create_image(width // 2, height // 2, image=self.photo)

    def poll(self) -> None:
        if self.closed:
            return
        if self.session.expire(time.monotonic()):
            self.finish()
        if self.worker is not None:
            while True:
                try:
                    kind, value = self.worker.events.get_nowait()
                except queue.Empty:
                    break
                if self.session.phase != Phase.RECEIVING:
                    continue
                if kind == "codes" and self.session.accept(value, time.monotonic()):
                    self.finish()
                elif kind == "error":
                    self.session.phase = Phase.ERROR
                    self.session.detail = value
                    self.finish()
            try:
                self.last_frame = self.worker.frames.get_nowait()
                self.redraw()
            except queue.Empty:
                pass
        receiving = self.session.phase == Phase.RECEIVING
        busy = self.worker is not None and self.worker.is_alive()
        self.start_button.configure(state="normal" if self.session.phase == Phase.REQUEST and not busy else "disabled")
        self.cancel_button.configure(state="normal" if receiving else "disabled")
        self.new_button.configure(state="disabled" if busy else "normal")
        self.camera_input.configure(state="disabled" if receiving else "normal")
        if receiving and self.session.deadline is not None:
            remaining = max(0, math.ceil(self.session.deadline - time.monotonic()))
            self.status.configure(text=f"第2步：等待板端回传码，还剩{remaining}秒")
        self.root.after(50, self.poll)

    def close(self) -> None:
        self.closed = True
        if self.worker is not None:
            self.worker.stop()
            self.worker.join(timeout=1)
        self.root.destroy()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="CamWallet：显示请求二维码，读取板端回传并逐字节比较。")
    parser.add_argument("--camera", type=int, default=0, help="电脑摄像头编号，默认0")
    parser.add_argument("--backend", choices=("auto", "dshow", "msmf"), default="auto", help="OpenCV摄像头后端")
    parser.add_argument("--timeout", type=float, default=30, help="读取超时秒数，默认30")
    parser.add_argument("--text", help="固定UTF-8测试文本；默认每轮生成新的随机测试内容")
    parser.add_argument("--self-test", action="store_true", help="运行软件测试，不打开窗口或摄像头")
    args = parser.parse_args(argv)
    if args.camera < 0:
        parser.error("摄像头编号不能为负数。")
    try:
        EchoSession(args.text.encode("utf-8") if args.text is not None else new_payload(), args.timeout)
    except (ValueError, UnicodeError) as error:
        parser.error(str(error))
    try:
        import cv2  # noqa: F401
        import qrcode  # noqa: F401
        import zxingcpp  # noqa: F401
        from PIL import ImageTk  # noqa: F401
    except ImportError as error:
        print(f"缺少运行依赖：{error}。请使用 companion/.venv 的 Python，并安装 requirements.txt。", file=sys.stderr)
        return 2
    if args.self_test:
        import unittest

        tests = unittest.defaultTestLoader.discover(str(Path(__file__).parent / "tests"))
        result = unittest.TextTestRunner(verbosity=2).run(tests)
        print("软件测试不代表实物摄像头回传验收。")
        return 0 if result.wasSuccessful() else 1
    try:
        import tkinter as tk
    except ImportError as error:
        print(f"缺少Tk支持：{error}。请使用带Tk的桌面Python运行。", file=sys.stderr)
        return 2
    try:
        root = tk.Tk()
        app = EchoWindow(root, args)
        root.mainloop()
    except tk.TclError as error:
        print(f"无法打开桌面窗口：{error}。请使用带Tk支持的桌面Python运行。", file=sys.stderr)
        return 2
    if app.session.phase == Phase.PASSED:
        return 0
    return 130 if app.session.phase in (Phase.REQUEST, Phase.RECEIVING, Phase.CANCELLED) else 1


if __name__ == "__main__":
    raise SystemExit(main())
