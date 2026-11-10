#!/usr/bin/env python3
import os
import sys
import time
import unittest

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

from gateway.serial_driver import SerialDriver
from gateway.stream_engine import StreamEngine
from gateway.tests.test_serial_driver import MockSerialConnection


class TestStreamEngine(unittest.TestCase):
    def setUp(self):
        self.mock_ser = MockSerialConnection()
        self.driver = SerialDriver(port="MOCK_PORT", poll_interval=0.05)
        self.driver.connect(mock_stream=self.mock_ser)
        self.engine = StreamEngine(self.driver)

    def tearDown(self):
        self.driver.disconnect()

    def test_z_feedrate_clamping(self):
        # Danger line: Z moved at 12000 mm/min
        unsafe = "G1 Z-4.50 F12000 ; Fast dive"
        safe = self.engine.sanitize_line(unsafe)
        self.assertEqual(safe, "G1 Z-4.50 F1000")

        # XY line should NOT be clamped
        xy_line = "G1 X-350.00 Y150.00 F12000"
        self.assertEqual(self.engine.sanitize_line(xy_line), xy_line)

    def test_m400_translation(self):
        self.assertEqual(self.engine.sanitize_line("M400"), "G4 P0.05")
        self.assertEqual(self.engine.sanitize_line("m400 ; sync"), "G4 P0.05")

    def test_comment_stripping(self):
        self.assertEqual(self.engine.sanitize_line("; pure comment"), "")
        self.assertEqual(self.engine.sanitize_line("(embedded comment)"), "")
        self.assertEqual(self.engine.sanitize_line("G0 X0 (move) ; comment"), "G0 X0")

    def test_job_streaming_flow(self):
        raw_gcode = """
        G21
        G90
        G0 Z-0.50 F1000
        G1 X-300 Y100 F12000
        G1 Z-4.50 F12000
        G0 Z-0.50 F1000
        """
        job = self.engine.prepare_job(raw_gcode, "TestJob")
        self.assertEqual(job["total_lines"], 6)

        progresses = []
        completed = []

        self.engine.register_progress_callback(lambda p: progresses.append(p))
        self.engine.register_complete_callback(lambda c: completed.append(c))

        ok, msg = self.engine.start_job(job)
        self.assertTrue(ok)

        # Wait for thread to finish (6 lines)
        time.sleep(0.3)
        self.assertFalse(self.engine.is_streaming)
        self.assertEqual(len(completed), 1)
        self.assertEqual(completed[0]["status"], "completed")
        self.assertTrue(len(progresses) >= 5)
        self.assertEqual(progresses[-1]["percent"], 100.0)

    def test_pause_and_resume(self):
        self.engine.is_streaming = True
        ok_pause = self.engine.pause()
        self.assertTrue(ok_pause)
        self.assertTrue(self.engine.is_paused)
        self.assertIn(b"!", self.mock_ser.written)

        ok_resume = self.engine.resume()
        self.assertTrue(ok_resume)
        self.assertFalse(self.engine.is_paused)
        self.assertIn(b"~", self.mock_ser.written)

    def test_abort(self):
        self.engine.is_streaming = True
        ok_abort = self.engine.abort()
        self.assertTrue(ok_abort)
        self.assertIn(b"\x18", self.mock_ser.written)


if __name__ == "__main__":
    unittest.main()

