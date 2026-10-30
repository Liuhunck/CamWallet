"""Bounded UR transport and unsigned EVM fixtures for the CamWallet demo."""
from __future__ import annotations

import math
import hashlib
import secrets
import sys
import uuid
from dataclasses import dataclass, field
from pathlib import Path

# The upstream package has absolute `ur.*` imports. Keep its sources intact.
sys.path.insert(0, str(Path(__file__).parent / "third_party" / "foundation_ur"))
from ur.ur import UR
from ur.ur_encoder import UREncoder
from ur.ur_decoder import URDecoder
from ur.bytewords import Bytewords, Bytewords_Style_minimal
from ur.fountain_encoder import Part
from qr_echo import Phase

MESSAGE_MAX = 768
PART_MAX = 128
FRAGMENT_BYTES = 24
SEQUENCE_MAX = 32


def cbor_head(major: int, number: int) -> bytes:
    if number < 0 or number >= 1 << 64:
        raise ValueError("CBOR integer out of range")
    if number < 24:
        return bytes([major << 5 | number])
    for count, ai in ((1, 24), (2, 25), (4, 26), (8, 27)):
        if number < 1 << (8 * count):
            return bytes([major << 5 | ai]) + number.to_bytes(count, "big")
    raise ValueError("CBOR integer out of range")


def cbor(value) -> bytes:
    if isinstance(value, bool):
        return b"\xf5" if value else b"\xf4"
    if isinstance(value, int):
        return cbor_head(0, value)
    if isinstance(value, bytes):
        return cbor_head(2, len(value)) + value
    if isinstance(value, str):
        raw = value.encode("utf-8")
        return cbor_head(3, len(raw)) + raw
    if isinstance(value, list):
        return cbor_head(4, len(value)) + b"".join(map(cbor, value))
    if isinstance(value, dict):
        return cbor_head(5, len(value)) + b"".join(cbor(k) + cbor(v) for k, v in value.items())
    raise TypeError(type(value))


def rlp(value) -> bytes:
    if isinstance(value, int):
        if value < 0:
            raise ValueError("negative RLP integer")
        value = value.to_bytes((value.bit_length() + 7) // 8, "big")
    is_list = isinstance(value, list)
    raw = b"".join(map(rlp, value)) if is_list else bytes(value)
    if not is_list and len(raw) == 1 and raw[0] < 128:
        return raw
    prefix = 0xc0 if is_list else 0x80
    if len(raw) < 56:
        return bytes([prefix + len(raw)]) + raw
    length = len(raw).to_bytes((len(raw).bit_length() + 7) // 8, "big")
    return bytes([prefix + 55 + len(length)]) + length + raw


def evm_demo_request(request_id: bytes | None = None, *, chain_id: int = 11155111,
                     tx_fields: list | None = None, data_type: int = 4,
                     path: list | None = None) -> bytes:
    """ERC-4527 fixture, no key, RPC, balance query, signature or broadcast."""
    request_id = uuid.uuid4().bytes if request_id is None else request_id
    fields = tx_fields if tx_fields is not None else [
        chain_id, 0, 1_000_000_000, 2_000_000_000, 21000,
        b"\x11" * 20, 10**15, b"", [],
    ]
    path = path if path is not None else [44, True, 60, True, 0, True, 0, False, 0, False]
    # Nested tags are written explicitly; request/signature top-level CBOR has
    # no extra tag when the type is already present in the UR envelope.
    entries = [
        cbor(1) + cbor_head(6, 37) + cbor(request_id),
        cbor(2) + cbor(b"\x02" + rlp(fields)),
        cbor(3) + cbor(data_type), cbor(4) + cbor(chain_id),
        cbor(5) + cbor_head(6, 304) + cbor({1: path}),
        cbor(7) + cbor("CamWallet demo"),
    ]
    return cbor_head(5, len(entries)) + b"".join(entries)


def new_ur_request(evm: bool = False, size: int = 256) -> tuple[str, bytes, bytes]:
    if evm:
        request_id = uuid.uuid4().bytes
        request = evm_demo_request(request_id)
        # Expected identity, not an expected decision: either explicit button
        # result is valid. Bind both the UUID and the complete CBOR contents.
        return "eth-sign-request", request, request_id + hashlib.sha256(request).digest()
    if not 16 <= size <= 765:
        raise ValueError("UR测试数据长度需为16–765字节。")
    request = cbor(b"CamWallet:UR:" + secrets.token_bytes(size - 13))
    return "bytes", request, request


def encoder(ur_type: str, payload: bytes) -> UREncoder:
    if not 1 <= len(payload) <= MESSAGE_MAX:
        raise ValueError("UR CBOR长度需为1–768字节。")
    return UREncoder(UR(ur_type, bytearray(payload)), FRAGMENT_BYTES,
                     min_fragment_len=min(10, len(payload)))


def checked_part(text: str) -> tuple[str, bool]:
    """Bound every reference-decoder allocation before accepting camera text."""
    if len(text) > PART_MAX or any(ord(c) < 33 or ord(c) > 126 for c in text):
        raise ValueError("UR frame length/characters")
    ur_type, components = URDecoder.parse(text)
    if ur_type != "bytes" or len(components) not in (1, 2):
        raise ValueError("Expected ur:bytes diagnostic response")
    decoded = Bytewords.decode(Bytewords_Style_minimal, components[-1])
    if len(components) == 2:
        part = Part.from_cbor(decoded)
        sequence = URDecoder.parse_sequence_component(components[0])
        if (sequence != (part.seq_num, part.seq_len) or not 1 <= part.seq_len <= SEQUENCE_MAX
                or not 1 <= part.message_len <= MESSAGE_MAX or not 1 <= part.seq_num < 1 << 32
                or not 0 <= part.checksum < 1 << 32
                or len(part.data) != math.ceil(part.message_len / part.seq_len)):
            raise ValueError("UR fragment exceeds board limits")
    elif not 1 <= len(decoded) <= MESSAGE_MAX:
        raise ValueError("UR message size")
    return ur_type, len(components) == 2


@dataclass
class URSession:
    expected: bytes
    timeout: float = 60.0
    review: bool = False
    decision: str | None = field(default=None, init=False)
    phase: Phase = field(default=Phase.REQUEST, init=False)
    received: tuple[bytes, ...] = field(default=(), init=False)
    detail: str = field(default="", init=False)
    deadline: float | None = field(default=None, init=False)
    decoder: URDecoder = field(default_factory=URDecoder, init=False)
    multipart: bool | None = field(default=None, init=False)

    def __post_init__(self):
        if not 1 <= len(self.expected) <= MESSAGE_MAX:
            raise ValueError("UR message too large")
        if self.review and len(self.expected) != 48:
            raise ValueError("Review identity must contain UUID and SHA-256")
        if not math.isfinite(self.timeout) or not 0 < self.timeout <= 300:
            raise ValueError("读取超时必须大于0且不超过300秒。")

    def begin(self, now: float):
        if self.phase != Phase.REQUEST:
            raise ValueError("Start a new session first")
        self.phase = Phase.RECEIVING
        self.deadline = now + self.timeout

    def part_counts(self) -> tuple[int, int]:
        """Multipart progress; the total is unknown before a valid fragment."""
        # Upstream expected_part_count() calls len(None) before the first
        # fountain fragment. Blank/invalid camera frames do not initialize it.
        if self.decoder.processed_parts_count() == 0:
            return 0, 0
        return (len(self.decoder.received_part_indexes()),
                self.decoder.expected_part_count())

    def expire(self, now: float) -> bool:
        if self.phase == Phase.RECEIVING and self.deadline is not None and now >= self.deadline:
            self.phase = Phase.TIMEOUT
            self.detail = "TIMEOUT：未收齐动态回传码，请调整距离后开始新一轮。"
            return True
        return False

    def accept(self, payloads: list[bytes], now: float) -> bool:
        self.expire(now)
        if self.phase != Phase.RECEIVING or not payloads:
            return False
        if len(payloads) != 1:
            self.phase = Phase.FAILED
            self.detail = "FAIL：画面中有多个二维码，请只保留板端屏幕。"
            return True
        try:
            text = payloads[0].decode("ascii")
            _, multipart = checked_part(text)
            if self.multipart is not None and self.multipart != multipart:
                return False
            self.decoder.receive_part(text)
            self.multipart = multipart
        except Exception:
            # Upstream uses several custom exception classes for malformed
            # URs. A bad camera frame must not terminate the receiver/UI.
            return False
        if self.decoder.is_failure():
            self.phase = Phase.FAILED
            self.detail = "FAIL：UR整包校验失败。"
            return True
        if not self.decoder.is_success():
            return False
        actual = bytes(self.decoder.result_message().cbor)
        self.received = (actual,)
        if self.review:
            # Exact canonical CBOR bstr + fixed versioned binary payload.
            # Reject old echo firmware, unknown versions/statuses and tails.
            valid = (len(actual) == 57 and actual[:8] == b"\x58\x37CWREV\x01"
                     and actual[8] in (0, 1) and actual[9:] == self.expected)
            self.phase = Phase.PASSED if valid else Phase.FAILED
            if valid:
                self.decision = "CONFIRMED" if actual[8] else "REJECTED"
                action = "已确认" if actual[8] else "已拒绝"
                self.detail = f"PASS：板端{action} ({self.decision})；请求编号和摘要一致。未签名、未广播。"
            else:
                self.detail = "FAIL：审核回执格式、请求编号或摘要不符；检查固件版本和是否为旧回传码。"
            return True
        self.phase = Phase.PASSED if actual == self.expected else Phase.FAILED
        self.detail = (f"PASS：完整UR消息{len(actual)}字节一致；这是传输/解析测试，未签名。"
                       if self.phase == Phase.PASSED else "FAIL：完整消息与本轮请求不一致，可能是旧回传码。")
        return True
