from __future__ import annotations

import argparse
import queue
import sys
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

import cv2
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from qr_echo import (  # noqa: E402
    CameraWorker, EchoSession, EchoWindow, Phase, decode_qrs,
    make_request_image, new_payload,
)


class QRImageTests(unittest.TestCase):
    def test_fresh_random_request_each_round(self):
        first, second = new_payload(), new_payload()
        self.assertNotEqual(first, second)
        self.assertEqual(len(first), 31)
        self.assertTrue(first.startswith(b"CamWallet:echo:"))

    def test_real_qr_roundtrip_preserves_bytes(self):
        values = [
            b"CamWallet:hello-12345", new_payload(), "你好，CamWallet".encode("utf-8"),
            b"\x00\xff\x80\x1b\n\r\\\"", bytes(range(128)),
        ]
        for value in values:
            with self.subTest(value=value):
                image = make_request_image(value)
                self.assertEqual(decode_qrs(image), [value])

    def test_rotation_and_bgr_camera_input(self):
        value = new_payload()
        image = np.array(make_request_image(value).resize((330, 330)))
        bgr = cv2.cvtColor(image, cv2.COLOR_RGB2BGR)
        self.assertEqual(decode_qrs(cv2.rotate(bgr, cv2.ROTATE_90_CLOCKWISE)), [value])

    def test_blank_camera_frame_has_no_result(self):
        self.assertEqual(decode_qrs(np.full((480, 640, 3), 255, dtype=np.uint8)), [])

    def test_byte_limits_before_generating_request(self):
        for value in (b"", b"x" * 129, "中".encode("utf-8") * 43):
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    make_request_image(value)


class EchoStateTests(unittest.TestCase):
    def test_outgoing_request_cannot_pass_before_receiver_is_armed(self):
        session = EchoSession(b"request")
        self.assertFalse(session.accept([b"request"], 10))
        self.assertEqual(session.phase, Phase.REQUEST)
        session.begin(10)
        self.assertTrue(session.accept([b"request"], 11))
        self.assertEqual(session.phase, Phase.PASSED)

    def test_wrong_or_old_response_fails(self):
        expected = new_payload()
        session = EchoSession(expected)
        session.begin(10)
        self.assertTrue(session.accept([b"CamWallet:hello-12345"], 11))
        self.assertEqual(session.phase, Phase.FAILED)

    def test_comparison_is_exact_and_does_not_normalize_unicode(self):
        session = EchoSession("é".encode("utf-8"))
        session.begin(10)
        self.assertTrue(session.accept(["e\u0301".encode("utf-8")], 11))
        self.assertEqual(session.phase, Phase.FAILED)

    def test_multiple_codes_do_not_pass_even_if_one_matches(self):
        session = EchoSession(b"expected")
        session.begin(10)
        self.assertTrue(session.accept([b"expected", b"other"], 11))
        self.assertEqual(session.phase, Phase.FAILED)

    def test_empty_frame_keeps_waiting_and_deadline_rejects_late_result(self):
        session = EchoSession(b"expected", timeout=5)
        session.begin(10)
        self.assertFalse(session.accept([], 11))
        self.assertEqual(session.phase, Phase.RECEIVING)
        self.assertFalse(session.accept([b"expected"], 15))
        self.assertEqual(session.phase, Phase.TIMEOUT)

    def test_invalid_timeout(self):
        for timeout in (0, -1, 301, float("nan"), float("inf")):
            with self.subTest(timeout=timeout):
                with self.assertRaises(ValueError):
                    EchoSession(b"x", timeout=timeout)

    def test_terminal_result_is_not_overwritten(self):
        session = EchoSession(b"expected")
        session.begin(10)
        session.accept([b"expected"], 11)
        self.assertFalse(session.accept([b"wrong"], 12))
        self.assertEqual(session.phase, Phase.PASSED)
        self.assertEqual(session.received, (b"expected",))


class CameraTests(unittest.TestCase):
    @staticmethod
    def events(worker):
        events = []
        while True:
            try:
                events.append(worker.events.get_nowait())
            except queue.Empty:
                return events

    def test_camera_release_after_success(self):
        value = new_payload()
        frame = cv2.cvtColor(np.array(make_request_image(value).resize((330, 330))), cv2.COLOR_RGB2BGR)
        capture = Mock()
        capture.isOpened.return_value = True
        capture.read.return_value = (True, frame)
        worker = CameraWorker(0, "auto")
        with patch("cv2.VideoCapture", return_value=capture):
            worker.run()
        self.assertIn(("codes", [value]), self.events(worker))
        capture.release.assert_called_once()

    def test_open_failure_is_reported_and_released(self):
        capture = Mock()
        capture.isOpened.return_value = False
        worker = CameraWorker(9, "auto")
        with patch("cv2.VideoCapture", return_value=capture):
            worker.run()
        self.assertTrue(any(kind == "error" for kind, _ in self.events(worker)))
        capture.release.assert_called_once()

    def test_decoder_exception_releases_camera(self):
        capture = Mock()
        capture.isOpened.return_value = True
        capture.read.return_value = (True, np.zeros((100, 100, 3), dtype=np.uint8))
        worker = CameraWorker(0, "auto")
        with patch("cv2.VideoCapture", return_value=capture), patch("qr_echo.decode_qrs", side_effect=RuntimeError("test decoder error")):
            worker.run()
        self.assertIn(("error", "test decoder error"), self.events(worker))
        capture.release.assert_called_once()

    def test_cancelled_worker_does_not_open_camera(self):
        worker = CameraWorker(0, "auto")
        worker.stop()
        with patch("cv2.VideoCapture") as factory:
            worker.run()
        factory.assert_not_called()


class WindowFlowTests(unittest.TestCase):
    def test_hides_and_paints_request_before_starting_camera(self):
        window = EchoWindow.__new__(EchoWindow)
        window.session = EchoSession(b"request")
        window.worker = None
        window.camera_index = Mock()
        window.camera_index.get.return_value = "0"
        window.args = argparse.Namespace(backend="auto")
        window.status = Mock()
        window.instructions = Mock()
        window.root = Mock()
        window.redraw = Mock()
        worker = Mock()
        order = Mock()
        order.attach_mock(window.redraw, "hide_request")
        order.attach_mock(window.root.update_idletasks, "paint_hidden_request")
        order.attach_mock(worker.start, "start_camera")
        with patch("qr_echo.CameraWorker", return_value=worker):
            window.start_receiving()
        self.assertEqual(window.session.phase, Phase.RECEIVING)
        self.assertEqual([call[0] for call in order.mock_calls], ["hide_request", "paint_hidden_request", "start_camera"])

    def test_receiver_redraw_cannot_restore_outgoing_qr(self):
        window = EchoWindow.__new__(EchoWindow)
        window.closed = False
        window.session = EchoSession(b"request")
        window.session.begin(10)
        window.request_image = make_request_image(b"request")
        window.last_frame = None
        window.canvas = Mock()
        window.canvas.winfo_width.return_value = 640
        window.canvas.winfo_height.return_value = 480
        window.redraw()
        window.canvas.delete.assert_called_once_with("all")
        window.canvas.create_image.assert_not_called()
        window.canvas.create_text.assert_called_once()


if __name__ == "__main__":
    unittest.main()
