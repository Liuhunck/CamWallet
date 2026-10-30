import hashlib
import subprocess
import unittest
from pathlib import Path

from qr_echo import Phase
from ur_protocol import URSession, cbor, encoder, evm_demo_request, new_ur_request

BRIDGE = Path(__file__).resolve().parents[2] / "firmware/tests/build/ur_bridge.exe"


class ReviewReceiptTests(unittest.TestCase):
    def setUp(self):
        self.uid = bytes(range(16))
        self.request = evm_demo_request(self.uid)
        self.identity = self.uid + hashlib.sha256(self.request).digest()

    def receipt(self, confirm=True):
        return cbor(b"CWREV\x01" + bytes([int(confirm)]) + self.identity)

    def receive(self, payload, session=None):
        session = session or URSession(self.identity, review=True)
        session.begin(0)
        enc = encoder("bytes", payload)
        for _ in range(100):
            if session.accept([enc.next_part().upper().encode()], 1):
                break
        return session

    def test_both_decisions_are_valid_but_distinct(self):
        for confirm in (False, True):
            session = self.receive(self.receipt(confirm))
            self.assertEqual(session.phase, Phase.PASSED)
            self.assertEqual(session.decision, "CONFIRMED" if confirm else "REJECTED")
            self.assertIn("未签名", session.detail)

    def test_reject_old_id_digest_version_status_and_trailing_data(self):
        receipt = self.receipt()
        for offset in (2, 7, 8, 9, 25, 56):
            bad = bytearray(receipt); bad[offset] ^= 0x80
            session = self.receive(bytes(bad))
            self.assertEqual(session.phase, Phase.FAILED, offset)
            self.assertIsNone(session.decision)
        for bad in (receipt[:-1], receipt + b"\x00", cbor(self.request)):
            self.assertEqual(self.receive(bad).phase, Phase.FAILED)

    def test_generated_request_identity_and_replay(self):
        _, request, expected = new_ur_request(True)
        self.assertEqual(expected[16:], hashlib.sha256(request).digest())
        self.assertIn(expected[:16], request)
        old = self.receive(self.receipt(), URSession(expected, review=True))
        self.assertEqual(old.phase, Phase.FAILED)

    @unittest.skipUnless(BRIDGE.exists(), "Build firmware/tests first")
    def test_c_receipts_match_independent_python_and_ur_transport(self):
        for status in ("confirm", "reject"):
            result = subprocess.run([str(BRIDGE), "decision", self.request.hex(), status],
                                    capture_output=True, text=True, check=True)
            data = bytes.fromhex(result.stdout.strip())
            self.assertEqual(data, self.receipt(status == "confirm"))
            # Cross the actual C encoder and reference decoder, not only bytes.
            result = subprocess.run([str(BRIDGE), "encode", "bytes", data.hex(), "50"],
                                    capture_output=True, text=True, check=True)
            session = URSession(self.identity, review=True); session.begin(0)
            for part in reversed(result.stdout.splitlines()):
                if session.accept([part.encode()], 1):
                    break
            self.assertEqual(session.phase, Phase.PASSED)
            self.assertIn("arena_used=0", result.stderr)
        for status in ("pending", "unknown"):
            result = subprocess.run([str(BRIDGE), "decision", self.request.hex(), status], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stdout, b"")

    @unittest.skipUnless(BRIDGE.exists(), "Build firmware/tests first")
    def test_c_review_renders_all_digits_without_truncation(self):
        for chain in (1, 11155111, (1 << 64) - 1):
            amount = (1 << 256) - 1
            fee = (1 << 64) - 1
            gas = (1 << 32) - 1
            request = evm_demo_request(self.uid, chain_id=chain, tx_fields=[
                chain, fee, fee, fee, gas, bytes(range(20)), amount, b"", []])
            result = subprocess.run([str(BRIDGE), "review", request.hex()],
                                    capture_output=True, text=True, check=True)
            rows = result.stdout.splitlines()
            self.assertEqual(len(rows), 60)
            self.assertTrue(all(len(row) <= 20 for row in rows))
            self.assertEqual(rows[4], str(chain))
            self.assertEqual(rows[7], str(fee))
            self.assertEqual("".join(rows[13:16]), "0x" + bytes(range(20)).hex())
            self.assertEqual("".join(rows[25:29]), str(amount))
            self.assertEqual("".join(rows[37:39]), str(fee * gas))
            if chain in (1, 11155111):
                self.assertEqual("".join(rows[31:35]), f"{amount // 10**18}.{amount % 10**18:018d}")
            else:
                self.assertIn("UNKNOWN NETWORK", rows)
                self.assertNotIn("AMOUNT (ETH):", rows)


if __name__ == "__main__":
    unittest.main()
