"""Sepolia: prepare -> optical signing -> explicit broadcast. Use .venv Python."""
from __future__ import annotations
import argparse
import json
import os
import sys
from decimal import Decimal, InvalidOperation
from pathlib import Path

from evm_signing import SigningSession, RPC, prepare, broadcast, fields
from qr_echo import Phase
from ur_echo import URWindow


def save_new(path: Path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write("\n")


class SigningWindow(URWindow):
    def __init__(self, root, args, tx, output):
        self.tx, self.output = tx, output
        super().__init__(root, args)
        root.title("CamWallet Sepolia 原生币签名")

    def make_session(self):
        session = SigningSession(self.tx, self.args.timeout)
        return "eth-sign-request", session.payload, session

    def new_round(self):
        super().new_round()
        self.instructions.configure(text="板端先 CREATE 创建账户。核对 FROM/TO、金额、费用后在第5页 CONFIRM 签名；看到 SIGNED 再点开始读取。REJECT 不签名。")

    def finish(self):
        super().finish()
        if self.session.phase == Phase.PASSED and self.session.raw_transaction is not None:
            artifact = {"version": 1, "transaction": self.session.tx,
                        "request_id": self.session.request_id.hex(),
                        "raw_transaction": "0x" + self.session.raw_transaction.hex(),
                        "tx_hash": self.session.tx_hash}
            try:
                save_new(self.output, artifact)
                self.instructions.configure(text=f"已保存验签通过的交易到 {self.output}。尚未广播；关闭窗口后运行 broadcast 命令。")
            except OSError as error:
                self.session.phase = Phase.ERROR
                self.session.detail = f"签名已核验，但保存失败：{error}。请使用新的 --out 路径。"
                self.status.configure(text=self.session.detail)
        else:
            self.instructions.configure(text="未生成可广播的交易。REJECT 表示板端拒绝；FAIL/TIMEOUT 可重新读取。账户已创建时不要 RESET，否则会丢失该账户。")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    p = commands.add_parser("prepare", help="查询 Sepolia 参数并保存未签名请求")
    p.add_argument("--sender", required=True, help="板端新生成的完整地址")
    p.add_argument("--to", required=True)
    p.add_argument("--amount-eth", required=True)
    p.add_argument("--out", type=Path, default=Path("companion/local/request.json"))
    s = commands.add_parser("sign", help="显示请求码，读取签名并独立验签；不广播")
    s.add_argument("request", type=Path)
    s.add_argument("--out", type=Path, default=Path("companion/local/signed.json"))
    s.add_argument("--camera", type=int, default=0)
    s.add_argument("--backend", choices=("auto", "dshow", "msmf"), default="auto")
    s.add_argument("--timeout", type=float, default=60)
    s.add_argument("--interval", type=int, default=400)
    b = commands.add_parser("broadcast", help="主动将已验签的交易广播到 Sepolia")
    b.add_argument("signed", type=Path)
    r = commands.add_parser("receipt", help="查询交易是否上链及执行结果")
    r.add_argument("tx_hash")
    args = parser.parse_args(argv)
    try:
        if args.command == "prepare":
            if args.out.exists():
                raise ValueError("输出文件已存在；请换 --out 路径，避免混用旧交易。")
            # Parse decimal text exactly, without the Decimal context rounding
            # large values or a binary floating point conversion.
            amount = Decimal(args.amount_eth)
            if not amount.is_finite() or amount < 0:
                raise ValueError("金额必须是非负有限数。")
            numerator, denominator = amount.as_integer_ratio()
            wei, remainder = divmod(numerator * 10**18, denominator)
            if remainder:
                raise ValueError("金额精度最多18位小数。")
            tx = prepare(RPC(os.environ.get("CAMWALLET_RPC_URL")), args.sender, args.to, wei)
            save_new(args.out, {"version": 1, "transaction": tx})
            print(json.dumps(tx, ensure_ascii=False, indent=2))
            print(f"已保存 {args.out}；尚未签名或广播。")
        elif args.command == "sign":
            data = json.loads(args.request.read_text(encoding="utf-8"))
            if data.get("version") != 1:
                raise ValueError("请求文件版本不支持。")
            fields(data["transaction"])
            if args.out.exists():
                raise ValueError("签名输出文件已存在；请换 --out 路径。")
            if args.camera < 0 or not 200 <= args.interval <= 2000:
                raise ValueError("相机编号需非负，发码间隔需为200–2000ms。")
            SigningSession(data["transaction"], args.timeout)
            args.evm, args.bytes = True, 256
            import tkinter as tk
            root = tk.Tk()
            app = SigningWindow(root, args, data["transaction"], args.out)
            root.mainloop()
            return 0 if app.session.phase == Phase.PASSED else 1
        elif args.command == "broadcast":
            data = json.loads(args.signed.read_text(encoding="utf-8"))
            # Show the deterministic hash BEFORE submitting. If the network
            # times out, the user can query this hash before deciding to retry.
            from evm_signing import verify_raw
            known_hash = verify_raw(bytes.fromhex(data["raw_transaction"].removeprefix("0x")), data["transaction"])
            print(f"待广播交易哈希：{known_hash}", flush=True)
            tx_hash = broadcast(RPC(os.environ.get("CAMWALLET_RPC_URL")), data)
            print(f"已提交：{tx_hash}\nhttps://sepolia.etherscan.io/tx/{tx_hash}")
        else:
            if len(args.tx_hash) != 66 or not args.tx_hash.startswith("0x"):
                raise ValueError("交易哈希必须为0x加64位十六进制。")
            bytes.fromhex(args.tx_hash[2:])
            rpc = RPC(os.environ.get("CAMWALLET_RPC_URL")); rpc.check_chain()
            receipt = rpc.call("eth_getTransactionReceipt", [args.tx_hash])
            if receipt is None:
                print("尚无回执：可能等待打包或节点尚未收到。")
            else:
                print("执行成功" if int(receipt["status"], 16) == 1 else "执行失败")
                print(json.dumps(receipt, indent=2))
    except (OSError, ValueError, KeyError, TypeError, InvalidOperation) as error:
        print(f"错误：{error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
