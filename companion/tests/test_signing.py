import hashlib
import json
import subprocess
import unittest
import tempfile
from pathlib import Path
from unittest.mock import Mock, patch

from eth_account import Account
from eth_account.hdaccount import key_from_seed
from eth_utils import keccak

from evm_signing import SigningSession, CHAIN_ID, CURVE_N, verify_raw, prepare, broadcast, fields
from ur_protocol import cbor, cbor_head, encoder, evm_demo_request
from qr_echo import Phase
from evm_wallet import SigningWindow

BRIDGE = Path(__file__).resolve().parents[2] / "firmware/tests/build/ur_bridge.exe"
# Public Hardhat fixture; never compiled into the board firmware.
SEED = hashlib.pbkdf2_hmac("sha512", b"test test test test test test test test test test test junk", b"mnemonic", 2048)
KEY = key_from_seed(SEED, "m/44'/60'/0'/0/0")
ADDRESS = Account.from_key(KEY).address


def transaction(**changes):
    tx = dict(chain_id=CHAIN_ID, sender=ADDRESS, to="0x" + "11" * 20,
              nonce=0, priority_fee=10**9, max_fee=2*10**9, gas=21000, value=10**15)
    return tx | changes


def signature_payload(session, key=KEY):
    tx = session.tx
    signed = Account.sign_transaction(dict(type=2, chainId=CHAIN_ID, nonce=tx["nonce"],
        to=tx["to"], value=tx["value"], gas=tx["gas"], maxFeePerGas=tx["max_fee"],
        maxPriorityFeePerGas=tx["priority_fee"]), key)
    compact = signed.r.to_bytes(32, "big") + signed.s.to_bytes(32, "big") + bytes([signed.v])
    payload = b"\xa2\x01\xd8\x25\x50" + session.request_id + b"\x02\x58\x41" + compact
    return payload, signed


def accept(session, kind, payload):
    session.begin(0)
    enc = encoder(kind, payload)
    for _ in range(150):
        if session.accept([enc.next_part().encode()], 1):
            break
    return session


class SigningTests(unittest.TestCase):
    def test_window_saves_only_verified_signature_and_never_broadcasts(self):
        for reject in (False, True):
            s = SigningSession(transaction())
            if reject:
                accept(s, "bytes", b"\x58\x37CWREV\x01\x00" + s.expected)
            else:
                accept(s, "eth-signature", signature_payload(s)[0])
            with tempfile.TemporaryDirectory() as temp:
                app = SigningWindow.__new__(SigningWindow)
                app.session = s; app.output = Path(temp) / "signed.json"
                app.worker = Mock(); app.status = Mock(); app.instructions = Mock(); app.actual_text = Mock()
                with patch("evm_wallet.RPC") as network, patch("builtins.print"):
                    app.finish()
                    network.assert_not_called()
                self.assertEqual(app.output.exists(), not reject)
                if not reject:
                    artifact = json.loads(app.output.read_text())
                    self.assertEqual(verify_raw(bytes.fromhex(artifact["raw_transaction"][2:]), artifact["transaction"]), s.tx_hash)

    def test_python_signature_transport_and_raw_transaction(self):
        s = SigningSession(transaction())
        payload, signed = signature_payload(s)
        accept(s, "eth-signature", payload)
        self.assertEqual(s.phase, Phase.PASSED)
        self.assertEqual(s.raw_transaction, bytes(signed.raw_transaction))
        self.assertEqual(s.tx_hash, "0x" + bytes(signed.hash).hex())

    def test_rejection_and_review_confirmation_are_not_signatures(self):
        for decision in (0, 1):
            s = SigningSession(transaction())
            accept(s, "bytes", b"\x58\x37CWREV\x01" + bytes([decision]) + s.expected)
            self.assertEqual(s.phase, Phase.PASSED if decision == 0 else Phase.FAILED)
            self.assertIsNone(s.raw_transaction)

    def test_wrong_uuid_signer_transaction_and_malformed_signatures(self):
        original = SigningSession(transaction(), request_id=bytes(16))
        payload, _ = signature_payload(original)
        for tx, uid in ((transaction(), b"x"*16), (transaction(value=2), bytes(16)),
                        (transaction(sender=Account.from_key(b"\x12"*32).address), bytes(16))):
            s = accept(SigningSession(tx, request_id=uid), "eth-signature", payload)
            self.assertEqual(s.phase, Phase.FAILED)
            self.assertIsNone(s.raw_transaction)
        # Invalid recovery ID, r=0, high-S, short/trailing CBOR.
        invalid = [payload[:-1] + b"\x02", payload[:24] + bytes(32) + payload[56:],
                   payload[:56] + (CURVE_N - 1).to_bytes(32, "big") + payload[-1:], payload[:-1], payload+b"x"]
        for item in invalid:
            s = accept(SigningSession(transaction(), request_id=bytes(16)), "eth-signature", item)
            self.assertEqual(s.phase, Phase.FAILED)

    def test_raw_transaction_tampering_and_chain_rejected(self):
        s = SigningSession(transaction()); payload, signed = signature_payload(s)
        verify_raw(bytes(signed.raw_transaction), s.tx)
        for tx in (transaction(value=3), transaction(chain_id=1), transaction(gas=0)):
            with self.assertRaises(ValueError): verify_raw(bytes(signed.raw_transaction), tx)
        with self.assertRaises(ValueError): fields(transaction(value=True))

    @unittest.skipUnless(BRIDGE.exists(), "Build firmware/tests first")
    def test_c_bip32_addresses_match_independent_python(self):
        for seed in (bytes(range(16)), bytes(range(32)), SEED):
            result = subprocess.run([str(BRIDGE), "wallet", seed.hex()], capture_output=True, text=True, check=True)
            expected = Account.from_key(key_from_seed(seed, "m/44'/60'/0'/0/0")).address
            self.assertEqual(result.stdout.strip(), expected[2:].lower())

    @unittest.skipUnless(BRIDGE.exists(), "Build firmware/tests first")
    def test_c_signature_matches_python_deterministic_signature(self):
        for value in (0, 10**15, (1 << 256)-1):
            s = SigningSession(transaction(value=value))
            result = subprocess.run([str(BRIDGE), "wallet", SEED.hex(), s.payload.hex(), "approve"],
                                    capture_output=True, text=True, check=True)
            data = bytes.fromhex(result.stdout.splitlines()[1])
            expected, signed = signature_payload(s)
            self.assertEqual(data, expected)
            # Actual firmware UR encoder -> independent receiver.
            parts = subprocess.run([str(BRIDGE), "encode", "eth-signature", data.hex(), "80"],
                                   capture_output=True, text=True, check=True).stdout.splitlines()
            s.begin(0)
            for part in reversed(parts[15:]):
                if s.accept([part.encode()], 1): break
            self.assertEqual(s.phase, Phase.PASSED)
            self.assertEqual(s.raw_transaction, bytes(signed.raw_transaction))

    @unittest.skipUnless(BRIDGE.exists(), "Build firmware/tests first")
    def test_c_no_signature_without_approval_or_for_wrong_account(self):
        s = SigningSession(transaction())
        wrong = SigningSession(transaction(sender="0x"+"22"*20))
        old_path = cbor_head(6, 304) + cbor({1: [44, True, 60, True, 0, True, 0, False, 0, False]})
        new_path = cbor_head(6, 304) + cbor({1: [44, True, 60, True, 0, True, 0, False, 0, False], 2: 1})
        for payload, approval in ((s.payload, "reject"), (wrong.payload, "approve"),
                                  (evm_demo_request(chain_id=1), "approve"),
                                  (s.payload.replace(old_path, new_path), "approve"),
                                  (s.payload + b"\x00", "approve")):
            result = subprocess.run([str(BRIDGE), "wallet", SEED.hex(), payload.hex(), approval],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(len(result.stdout.splitlines()), 1) # Address only.


class RPCTests(unittest.TestCase):
    def test_prepare_queries_and_funds_check(self):
        rpc = Mock()
        answers = {"eth_getCode": "0x", "eth_getTransactionCount": "0x3", "eth_maxPriorityFeePerGas": "0x1",
                   "eth_getBlockByNumber": {"baseFeePerGas": "0xa"}, "eth_estimateGas": "0x5208",
                   "eth_getBalance": hex(10**18)}
        rpc.call.side_effect = lambda method, params: answers[method]
        tx = prepare(rpc, ADDRESS, "0x"+"11"*20, 1)
        self.assertEqual((tx["nonce"], tx["max_fee"], tx["gas"]), (3, 21, 21000))
        answers["eth_getBalance"] = "0x0"
        with self.assertRaises(ValueError): prepare(rpc, ADDRESS, tx["to"], 1)
        answers["eth_getCode"] = "0x1234"
        with self.assertRaises(ValueError): prepare(rpc, ADDRESS, tx["to"], 0)

    def artifact(self):
        s = SigningSession(transaction()); p, _ = signature_payload(s)
        accept(s, "eth-signature", p)
        return {"version": 1, "transaction": s.tx, "raw_transaction": "0x"+s.raw_transaction.hex(), "tx_hash": s.tx_hash}

    def test_explicit_broadcast_validates_then_sends_once(self):
        artifact = self.artifact(); rpc = Mock()
        rpc.call.side_effect = ["0x0", artifact["tx_hash"]]
        self.assertEqual(broadcast(rpc, artifact), artifact["tx_hash"])
        self.assertEqual([c.args[0] for c in rpc.call.call_args_list], ["eth_getTransactionCount", "eth_sendRawTransaction"])

    def test_no_send_on_wrong_chain_nonce_or_tampered_file(self):
        artifact = self.artifact()
        rpc = Mock(); rpc.check_chain.side_effect = ValueError("wrong chain")
        with self.assertRaises(ValueError): broadcast(rpc, artifact)
        rpc.call.assert_not_called()
        rpc = Mock(); rpc.call.return_value = "0x1"
        with self.assertRaises(ValueError): broadcast(rpc, artifact)
        self.assertEqual(rpc.call.call_count, 1)
        rpc = Mock(); artifact["transaction"]["value"] = 2
        with self.assertRaises(ValueError): broadcast(rpc, artifact)
        rpc.call.assert_not_called()

    def test_timeout_never_retries_submission(self):
        rpc = Mock(); rpc.call.side_effect = ["0x0", ValueError("timeout")]
        with self.assertRaises(ValueError): broadcast(rpc, self.artifact())
        self.assertEqual(rpc.call.call_count, 2)


if __name__ == "__main__": unittest.main()
