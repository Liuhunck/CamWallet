"""Sepolia-only transaction preparation and independent signature validation."""
from __future__ import annotations
import hashlib
import json
import urllib.request
import urllib.error
import uuid

from eth_account import Account
from eth_keys import keys
from eth_utils import keccak, to_checksum_address, is_address
import rlp as rlp_codec

from qr_echo import Phase
from ur_protocol import URSession, cbor, cbor_head, rlp

CHAIN_ID = 11155111
CURVE_N = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141


def address(value: str) -> str:
    if not isinstance(value, str) or not is_address(value):
        raise ValueError("地址必须是完整的 0x + 40 位十六进制。")
    result = to_checksum_address(value)
    raw = value[2:]
    if raw != raw.lower() and raw != raw.upper() and value != result:
        raise ValueError("混合大小写地址的 EIP-55 校验不符。")
    return result


def fields(tx: dict) -> list:
    required = {"chain_id", "nonce", "priority_fee", "max_fee", "gas", "to", "value", "sender"}
    if set(tx) != required:
        raise ValueError("交易字段缺失或包含未知字段。")
    limits = {"chain_id": 64, "nonce": 64, "priority_fee": 64, "max_fee": 64, "gas": 32, "value": 256}
    for name, bits in limits.items():
        if type(tx[name]) is not int or not 0 <= tx[name] < 1 << bits:
            raise ValueError(f"{name} 超出板端范围。")
    if tx["chain_id"] != CHAIN_ID:
        raise ValueError("当前仅支持 Sepolia，chain ID 11155111。")
    if tx["gas"] < 21000 or tx["max_fee"] < tx["priority_fee"]:
        raise ValueError("gas 或费用参数无效。")
    address(tx["sender"])
    return [CHAIN_ID, tx["nonce"], tx["priority_fee"], tx["max_fee"], tx["gas"],
            bytes.fromhex(address(tx["to"])[2:]), tx["value"], b"", []]


def request(tx: dict, request_id: bytes) -> tuple[bytes, bytes]:
    if len(request_id) != 16:
        raise ValueError("UUID must have 16 bytes")
    unsigned = b"\x02" + rlp(fields(tx))
    path = [44, True, 60, True, 0, True, 0, False, 0, False]
    entries = [cbor(1) + cbor_head(6, 37) + cbor(request_id),
               cbor(2) + cbor(unsigned), cbor(3) + cbor(4), cbor(4) + cbor(CHAIN_ID),
               cbor(5) + cbor_head(6, 304) + cbor({1: path}),
               cbor(6) + cbor(bytes.fromhex(address(tx["sender"])[2:])),
               cbor(7) + cbor("CamWallet Sepolia")]
    return cbor_head(5, len(entries)) + b"".join(entries), unsigned


def verify_raw(raw: bytes, tx: dict) -> str:
    expected = rlp_codec.decode(rlp(fields(tx)))
    if not raw or raw[0] != 2 or len(raw) > 768:
        raise ValueError("不是受支持的 Type 2 交易。")
    decoded = rlp_codec.decode(raw[1:], strict=True)
    if len(decoded) != 12 or decoded[:9] != expected or rlp_codec.encode(decoded) != raw[1:]:
        raise ValueError("签名交易内容与请求不一致。")
    for number in decoded[9:]:
        if not isinstance(number, bytes) or len(number) > 32 or (number and number[0] == 0):
            raise ValueError("签名整数编码不规范。")
    v, r, s = (int.from_bytes(n, "big") for n in decoded[9:])
    if v not in (0, 1) or not 0 < r < CURVE_N or not 0 < s <= CURVE_N // 2:
        raise ValueError("签名范围或 low-S 校验不符。")
    sender = Account.recover_transaction(raw)
    if sender != address(tx["sender"]):
        raise ValueError("签名恢复出的发送地址与设备地址不一致。")
    return "0x" + keccak(raw).hex()


class SigningSession(URSession):
    def __init__(self, tx: dict, timeout=60, request_id=None):
        # Copy to prevent a later caller mutation from changing verification.
        self.tx = dict(tx)
        self.request_id = uuid.uuid4().bytes if request_id is None else request_id
        self.payload, self.unsigned = request(self.tx, self.request_id)
        identity = self.request_id + hashlib.sha256(self.payload).digest()
        super().__init__(identity, timeout)
        self.allowed_types = ("bytes", "eth-signature")
        self.raw_transaction = None
        self.tx_hash = None

    def complete(self, actual: bytes, ur_type: str):
        self.phase = Phase.FAILED
        try:
            if ur_type == "bytes":
                if actual != b"\x58\x37CWREV\x01\x00" + self.expected:
                    raise ValueError("不是本轮的拒绝回执；审核版确认回执不能当签名。")
                self.phase = Phase.PASSED
                self.decision = "REJECTED"
                self.detail = "已拒绝：本轮回执匹配，没有签名或广播。"
                return
            if (len(actual) != 89 or actual[:5] != b"\xa2\x01\xd8\x25\x50"
                    or actual[5:21] != self.request_id or actual[21:24] != b"\x02\x58\x41"):
                raise ValueError("签名格式或请求 UUID 不符。")
            signature = actual[24:]
            r = int.from_bytes(signature[:32], "big")
            s = int.from_bytes(signature[32:64], "big")
            v = signature[64]
            if v not in (0, 1) or not 0 < r < CURVE_N or not 0 < s <= CURVE_N // 2:
                raise ValueError("签名范围或 low-S 校验不符。")
            recovered = keys.Signature(signature_bytes=signature).recover_public_key_from_msg_hash(keccak(self.unsigned))
            if recovered.to_checksum_address() != address(self.tx["sender"]):
                raise ValueError("签名与本轮交易或发送地址不符。")
            raw = b"\x02" + rlp(fields(self.tx) + [v, r, s])
            tx_hash = verify_raw(raw, self.tx)
            self.raw_transaction, self.tx_hash = raw, tx_hash
            self.phase, self.decision = Phase.PASSED, "SIGNED"
            self.detail = f"PASS：签名、发送地址和交易内容一致；尚未广播。交易哈希 {tx_hash}"
        except Exception as error:
            self.detail = f"FAIL：{error}"


class RPC:
    def __init__(self, url):
        if not url or not url.startswith(("https://", "http://localhost:", "http://127.0.0.1:")):
            raise ValueError("请设置 CAMWALLET_RPC_URL 为 Sepolia HTTPS RPC（本地测试允许 HTTP）。")
        self.url = url

    def call(self, method, params):
        data = json.dumps({"jsonrpc": "2.0", "id": 1, "method": method, "params": params}).encode()
        req = urllib.request.Request(self.url, data=data, headers={"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=20) as response:
                raw = response.read(1_048_577)
            if len(raw) > 1_048_576:
                raise ValueError("RPC响应过大。")
            result = json.loads(raw)
        except (urllib.error.URLError, TimeoutError, json.JSONDecodeError) as error:
            raise ValueError(f"RPC 请求失败 ({type(error).__name__})，请检查网络和 RPC 配置。") from None
        if not isinstance(result, dict) or result.get("id") != 1 or "error" in result or "result" not in result:
            raise ValueError(f"RPC 拒绝或返回异常：{method}")
        return result["result"]

    def check_chain(self):
        if int(self.call("eth_chainId", []), 16) != CHAIN_ID:
            raise ValueError("RPC 不是 Sepolia；未发送交易。")


def prepare(rpc: RPC, sender: str, to: str, value: int) -> dict:
    sender, to = address(sender), address(to)
    if type(value) is not int or not 0 <= value < 1 << 256:
        raise ValueError("金额超出 uint256 范围。")
    rpc.check_chain()
    # A plain ETH transfer to a contract may execute code. Keep this first
    # milestone to EOAs, matching the device's native-transfer-only review.
    if rpc.call("eth_getCode", [to, "latest"]) != "0x":
        raise ValueError("首版只向普通账户转账；收款地址含合约代码。")
    nonce = int(rpc.call("eth_getTransactionCount", [sender, "pending"]), 16)
    tip = int(rpc.call("eth_maxPriorityFeePerGas", []), 16)
    base = int(rpc.call("eth_getBlockByNumber", ["latest", False])["baseFeePerGas"], 16)
    tx = dict(chain_id=CHAIN_ID, sender=sender, to=to, value=value, nonce=nonce,
              priority_fee=tip, max_fee=2 * base + tip, gas=21000)
    gas = int(rpc.call("eth_estimateGas", [{"from": sender, "to": to, "value": hex(value),
        "maxFeePerGas": hex(tx["max_fee"]), "maxPriorityFeePerGas": hex(tip)}]), 16)
    tx["gas"] = max(21000, gas)
    fields(tx)
    balance = int(rpc.call("eth_getBalance", [sender, "pending"]), 16)
    if balance < value + tx["gas"] * tx["max_fee"]:
        raise ValueError("Sepolia 测试 ETH 不足以覆盖金额与最高费用。")
    return tx


def broadcast(rpc: RPC, artifact: dict) -> str:
    if artifact.get("version") != 1:
        raise ValueError("签名文件版本不支持。")
    raw = bytes.fromhex(artifact["raw_transaction"].removeprefix("0x"))
    tx = artifact["transaction"]
    tx_hash = verify_raw(raw, tx)
    if artifact["tx_hash"] != tx_hash:
        raise ValueError("签名文件交易哈希不符。")
    rpc.check_chain()
    if int(rpc.call("eth_getTransactionCount", [address(tx["sender"]), "pending"]), 16) != tx["nonce"]:
        raise ValueError("nonce 已变化，请重新准备并审核交易。")
    # Never retry eth_sendRawTransaction automatically after an ambiguous timeout.
    returned = rpc.call("eth_sendRawTransaction", ["0x" + raw.hex()])
    if not isinstance(returned, str) or returned.lower() != tx_hash:
        raise ValueError(f"节点返回的哈希异常；请先查询已知交易哈希 {tx_hash}，勿自动重发。")
    return tx_hash
