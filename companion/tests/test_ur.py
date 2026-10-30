from __future__ import annotations
import json
import queue
import random
import subprocess
import sys
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "companion"))
from ur_protocol import (UR, URDecoder, UREncoder, URSession, Part, Bytewords,
                         Bytewords_Style_minimal, cbor, cbor_head, encoder,
                         evm_demo_request, new_ur_request)
from qr_echo import Phase, decode_qrs, make_request_image, CameraWorker
from ur_echo import URWindow


def frames(kind, data, count=120, start=0):
    enc = UREncoder(UR(kind, bytearray(data)), 24, first_seq_num=start,
                    min_fragment_len=min(10, len(data)))
    return [enc.next_part().upper() for _ in range(count)]


class URSessionTests(unittest.TestCase):
    def test_reference_fragments_are_qr_readable(self):
        _, request, _ = new_ur_request()
        for text in frames("bytes", request, 3):
            self.assertLessEqual(len(text), 128)
            self.assertEqual(decode_qrs(make_request_image(text.encode())), [text.encode()])

    def test_complete_not_first_frame_and_duplicate_not_progress(self):
        data = cbor(bytes(range(256)))
        session = URSession(data)
        parts = frames("bytes", data)
        self.assertFalse(session.accept([parts[0].encode()], 1))
        session.begin(1)
        self.assertFalse(session.accept([parts[0].encode()], 2))
        self.assertFalse(session.accept([parts[0].encode()], 3))
        self.assertEqual(len(session.decoder.received_part_indexes()), 1)
        for part in parts[1:]:
            if session.accept([part.encode()], 4):
                break
        self.assertEqual(session.phase, Phase.PASSED)

    def test_oversized_or_unrelated_camera_codes_do_not_crash(self):
        session = URSession(cbor(b"CamWallet test"))
        session.begin(1)
        values = [b"hello", b"\xff", b"UR:", b"UR:BYTES/zz", b"UR:ETH-SIGNATURE/abcd", b"UR:" + b"A" * 200]
        values += [p.encode() for p in frames("bytes", b"x" * 769, 2)]
        for value in values:
            self.assertFalse(session.accept([value], 2))
        self.assertEqual(session.phase, Phase.RECEIVING)

    def test_old_completed_message_fails(self):
        session = URSession(cbor(b"new request!!")); session.begin(1)
        for part in frames("bytes", cbor(b"old request!!"), 3):
            session.accept([part.encode()], 2)
        self.assertEqual(session.phase, Phase.FAILED)

    def test_deadline_rejects_late_complete(self):
        data = cbor(b"small request")
        session = URSession(data, 5); session.begin(1)
        self.assertFalse(session.accept([frames("bytes", data, 1)[0].encode()], 6))
        self.assertEqual(session.phase, Phase.TIMEOUT)

    def test_multiple_qrs_fail(self):
        session = URSession(cbor(b"small request")); session.begin(1)
        self.assertTrue(session.accept([b"a", b"b"], 2))
        self.assertEqual(session.phase, Phase.FAILED)

    def test_new_requests_have_fresh_identity(self):
        self.assertNotEqual(new_ur_request()[1], new_ur_request()[1])
        self.assertNotEqual(new_ur_request(True)[1], new_ur_request(True)[1])
        self.assertEqual(new_ur_request(size=765)[1][:3], b"\x59\x02\xfd")

    def test_hide_request_before_continuous_camera(self):
        app = URWindow.__new__(URWindow)
        app.session = URSession(cbor(b"small request"))
        app.worker = None; app.camera_index = Mock(); app.camera_index.get.return_value = "0"
        app.args = Mock(backend="auto"); app.instructions = Mock(); app.root = Mock()
        order = []
        app.redraw = lambda: order.append(("hide", app.session.phase))
        app.root.update_idletasks.side_effect = lambda: order.append(("paint", app.session.phase))
        with patch("ur_echo.CameraWorker") as worker:
            worker.return_value.start.side_effect = lambda: order.append(("camera", app.session.phase))
            app.start_receiving()
            worker.assert_called_once_with(0, "auto", continuous=True)
        self.assertEqual([v[0] for v in order], ["hide", "paint", "camera"])
        self.assertTrue(all(v[1] == Phase.RECEIVING for v in order))


class URWindowPollingTests(unittest.TestCase):
    """Exercise the button handler and the real inherited polling path.

    Only widgets/camera are mocked; UR decoding, event queues, deadlines and
    terminal UI updates run normally, including the wait before the first QR.
    """

    def setUp(self):
        self.app = URWindow.__new__(URWindow)
        self.app.closed = False
        self.app.worker = None
        self.app.animation = None
        self.app.session = URSession(cbor(bytes(range(128))))
        self.app.args = Mock(backend="auto", evm=False, bytes=256, timeout=60, interval=400)
        for name in ("root", "status", "instructions", "redraw", "camera_index",
                     "camera_input", "start_button", "cancel_button", "new_button",
                     "expected_text", "actual_text"):
            setattr(self.app, name, Mock())
        self.app.camera_index.get.return_value = "0"
        self.clock = patch("ur_echo.time.monotonic", return_value=100.0).start()
        self.addCleanup(patch.stopall)
        patch("builtins.print").start()

    def begin(self):
        worker = Mock()
        worker.events = queue.SimpleQueue()
        worker.frames = queue.Queue()
        worker.is_alive.return_value = True
        with patch("ur_echo.CameraWorker", return_value=worker):
            self.app.start_receiving()
        return worker

    def status(self):
        return self.app.status.configure.call_args.kwargs["text"]

    def test_poll_before_first_valid_fragment(self):
        worker = self.begin()
        self.app.poll()
        self.assertIn("等待首个有效UR分片", self.status())
        for payload in (b"hello", b"UR:BYTES/ZZ", b"\xff"):
            worker.events.put(("codes", [payload]))
            self.app.poll()
            self.assertIn("等待首个有效UR分片", self.status())
        self.assertEqual(self.app.session.phase, Phase.RECEIVING)
        self.app.cancel_button.configure.assert_called_with(state="normal")
        self.app.root.after.assert_called_with(50, self.app.poll)

    def test_evm_new_round_and_both_receipt_results_in_real_poll(self):
        for confirmed in (False, True):
            self.app.worker = None
            self.app.args.evm = True
            self.app.new_round()
            self.assertTrue(self.app.session.review)
            identity = self.app.session.expected
            worker = self.begin()
            self.app.poll()
            self.assertIn("等待首个有效UR分片", self.status())
            receipt = cbor(b"CWREV\x01" + bytes([int(confirmed)]) + identity)
            for part in frames("bytes", receipt):
                worker.events.put(("codes", [part.encode()]))
                self.app.poll()
                if self.app.session.phase == Phase.PASSED:
                    break
            self.assertEqual(self.app.session.phase, Phase.PASSED)
            self.assertIn("CONFIRMED" if confirmed else "REJECTED", self.status())
            worker.stop.assert_called_once()

    def test_poll_updates_progress_duplicates_and_pass(self):
        worker = self.begin()
        self.app.poll()
        data = self.app.session.expected
        enc = encoder("bytes", data)
        parts = [enc.next_part().encode() for _ in range(enc.fountain_encoder.seq_len())]
        for payload in (parts[0], parts[0]):
            worker.events.put(("codes", [payload]))
            self.app.poll()
            self.assertIn(f"1/{len(parts)}", self.status())
            self.assertEqual(self.app.session.phase, Phase.RECEIVING)
        for payload in parts[1:]:
            worker.events.put(("codes", [payload]))
            self.app.poll()
        self.assertEqual(self.app.session.phase, Phase.PASSED)
        self.assertTrue(self.status().startswith("PASS"))
        self.assertEqual(self.app.session.received, (data,))
        worker.stop.assert_called_once()

    def test_poll_single_part_completes_without_fountain_count(self):
        data = cbor(b"small request")
        self.app.session = URSession(data)
        worker = self.begin()
        self.app.poll()
        worker.events.put(("codes", [frames("bytes", data, 1)[0].encode()]))
        self.app.poll()
        self.assertEqual(self.app.session.phase, Phase.PASSED)
        self.assertTrue(self.status().startswith("PASS"))

    def test_timeout_then_new_round_waits_for_first_fragment(self):
        worker = self.begin()
        self.app.poll()
        self.clock.return_value = 160.0
        self.app.poll()
        self.assertEqual(self.app.session.phase, Phase.TIMEOUT)
        self.assertTrue(self.status().startswith("TIMEOUT"))
        worker.stop.assert_called_once()
        worker.is_alive.return_value = False
        self.app.new_round()
        self.begin()
        self.app.poll()
        self.assertEqual(self.app.session.phase, Phase.RECEIVING)
        self.assertIn("等待首个有效UR分片", self.status())


class CInteropTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bridge = ROOT / "firmware/tests/build" / ("ur_bridge.exe" if sys.platform == "win32" else "ur_bridge")
        if not cls.bridge.exists():
            raise unittest.SkipTest("先构建firmware/tests以运行独立C/Python互通测试")

    def call(self, *args, input=None, ok=True):
        run = subprocess.run([str(self.bridge), *args], input=input, text=True,
                             capture_output=True, timeout=20)
        self.assertIn("arena_used=0", run.stderr)
        if ok:
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        else:
            self.assertEqual(run.returncode, 2, run.stdout + run.stderr)
        return run

    def decode(self, parts, data, kind="bytes"):
        run = self.call("decode", input="\n".join(parts) + "\n")
        self.assertEqual(run.stdout.strip(), f"{kind} {data.hex()}")

    def test_python_to_c_single_upper_lower(self):
        for content in (b"", b"x", b"small request"):
            data = cbor(content)
            text = frames("bytes", data, 1)[0]
            self.decode([text], data); self.decode([text.lower()], data)

    def test_python_to_c_reverse_duplicates_and_foreign_stream(self):
        data = cbor(bytes(range(256)))
        parts = frames("bytes", data, 11)
        foreign = frames("bytes", cbor(b"x" * 256), 3)
        self.decode([parts[-1], parts[-1], *foreign, *reversed(parts[:-1])], data)

    def test_python_to_c_fountain_only_with_loss(self):
        for length in (128, 256, 765):
            data = cbor(bytes(i % 251 for i in range(length)))
            parts = frames("bytes", data, 180, start=40)
            # No systematic frames at all; additionally drop every third frame.
            self.decode([p for i, p in enumerate(parts) if i % 3], data)

    def test_c_to_python_fountain_only(self):
        for length in (0, 1, 13, 256, 765):
            data = cbor(bytes(i % 251 for i in range(length)))
            parts = self.call("encode", "bytes", data.hex(), "180").stdout.splitlines()
            decoder = URDecoder()
            for part in parts[40:]:
                self.assertLessEqual(len(part), 128)
                decoder.receive_part(part)
                if decoder.is_complete(): break
            self.assertTrue(decoder.is_success())
            self.assertEqual(bytes(decoder.result_message().cbor), data)

    def test_encoder_matches_reference_selection(self):
        data = cbor(bytes(range(256)))
        actual = self.call("encode", "bytes", data.hex(), "80").stdout.splitlines()
        self.assertEqual(actual, frames("bytes", data, 80))

    def test_bad_bytewords_does_not_poison_valid_session(self):
        data = cbor(bytes(range(128))); parts = frames("bytes", data, 10)
        broken = parts[0][:-2] + "ZZ"
        self.decode([broken, parts[0], broken, *parts[1:]], data)

    def test_size_and_fragment_count_caps(self):
        for data in (b"x" * 769, b"x" * 2000):
            result = self.call("decode", input=frames("bytes", data, 1)[0] + "\n", ok=False)
            self.assertIn("TOO LARGE", result.stderr)

    def test_whole_message_crc_rejects(self):
        data = cbor(bytes(range(128))); modified = []
        for text in frames("bytes", data, 6):
            part = Part.from_cbor(Bytewords.decode(Bytewords_Style_minimal, text.split("/")[-1]))
            part.checksum ^= 1
            modified.append(UREncoder.encode_part("bytes", part))
        result = self.call("decode", input="\n".join(modified) + "\n", ok=False)
        self.assertIn("CRC ERROR", result.stderr)

    def test_evm_transport_type_is_not_bitcoin_specific(self):
        data = evm_demo_request(bytes(range(16)))
        self.decode(frames("eth-sign-request", data, 90, start=40), data, "eth-sign-request")

    def test_evm_valid_fields_and_max_value(self):
        request_id = bytes(range(16))
        for chain in (1, 11155111, 137, 8453):
            fields = [chain, 7, 10**9, 2*10**9, 21000, b"\x11"*20, 2**256-1, b"", []]
            result = json.loads(self.call("evm", evm_demo_request(request_id, chain_id=chain, tx_fields=fields).hex()).stdout)
            self.assertEqual(result, dict(request_id=request_id.hex(), chain_id=chain, nonce=7,
                to="11"*20, value=str(2**256-1), gas_limit=21000, max_total_fee=str(21000*2*10**9)))

    def test_evm_rejects_unsupported_and_malformed(self):
        base = [1, 0, 10**9, 2*10**9, 21000, b"\x11"*20, 10**15, b"", []]
        variants = []
        for index, value in [(0,2), (1,2**64), (2,3*10**9), (3,2**64), (4,20999),
                             (4,2**32), (5,b""), (5,b"\x11"*21), (6,2**256),
                             (6,b"\0"), (7,b"\x12"), (8,[[b"\x11"*20,[]]])]:
            fields = base.copy(); fields[index] = value
            variants.append(evm_demo_request(chain_id=1, tx_fields=fields))
        variants.append(evm_demo_request(chain_id=1, tx_fields=base + [0,1,2]))
        variants += [evm_demo_request(data_type=t) for t in (1,2,3)]
        variants += [evm_demo_request(path=[44,True,0,True,0,True,0,False,0,False]),
                     evm_demo_request(b"short")]
        good = evm_demo_request(bytes(range(16)))
        variants += [good[:n] for n in (1,3,20,len(good)-1)]
        variants += [good + b"\0", b"\xa7" + good[1:] + b"\x03\x04",
                     b"\xb8\x06" + good[1:]]
        for data in variants:
            with self.subTest(data=data.hex()):
                self.call("evm", data.hex(), ok=False)

    def test_random_malformed_cbor_is_rejected_without_leaks(self):
        rng = random.Random(4527)
        for _ in range(40):
            data = b"\xa6" + rng.randbytes(rng.randrange(1, 200))
            self.call("evm", data.hex(), ok=False)


if __name__ == "__main__":
    unittest.main()
