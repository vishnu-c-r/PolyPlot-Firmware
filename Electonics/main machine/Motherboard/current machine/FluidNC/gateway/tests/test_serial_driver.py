#!/usr/bin/env python3
import io
import os
import queue
import sys
import threading
import time
import unittest

# Ensure repo root is on sys.path
REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

from gateway.serial_driver import SerialDriver


class MockSerialConnection:
    def __init__(self):
        self.in_queue = queue.Queue()
        self.written = []
        self.closed = False

    def write(self, data):
        self.written.append(data)
        # Mock immediate reply to common commands
        if data == b"?":
            self.in_queue.put(b"<Run|MPos:-340.500,145.200,-4.500|FS:12000.0,0>\n")
        elif b"\n" in data:
            cmd = data.strip().decode()
            if "ERR" in cmd:
                self.in_queue.put(b"error:20\n")
            else:
                self.in_queue.put(b"ok\n")

    def readline(self):
        try:
            return self.in_queue.get(timeout=0.5)
        except queue.Empty:
            return b""

    def read_all(self):
        chunks = []
        while not self.in_queue.empty():
            chunks.append(self.in_queue.get_nowait())
        return b"".join(chunks)

    def close(self):
        self.closed = True


class TestSerialDriver(unittest.TestCase):
    def setUp(self):
        self.mock_ser = MockSerialConnection()
        self.driver = SerialDriver(port="MOCK_PORT", baud=115200, poll_interval=0.05)
        ok, msg = self.driver.connect(mock_stream=self.mock_ser)
        self.assertTrue(ok)

    def tearDown(self):
        self.driver.disconnect()

    def test_send_valid_line(self):
        success, resp = self.driver.send_line("G0 X-350 Y150 F12000")
        self.assertTrue(success)
        self.assertEqual(resp, "ok")
        self.assertIn(b"G0 X-350 Y150 F12000\n", self.mock_ser.written)

    def test_send_error_line(self):
        success, resp = self.driver.send_line("G0 ERR_COMMAND")
        self.assertFalse(success)
        self.assertIn("error:20", resp)

    def test_send_comment_skips(self):
        success, resp = self.driver.send_line("; This is a pure comment")
        self.assertTrue(success)
        self.assertEqual(resp, "ok")

    def test_realtime_command(self):
        ok = self.driver.send_realtime("!")
        self.assertTrue(ok)
        self.assertIn(b"!", self.mock_ser.written)

        ok_reset = self.driver.send_realtime(0x18)
        self.assertTrue(ok_reset)
        self.assertIn(b"\x18", self.mock_ser.written)

    def test_status_parsing(self):
        raw = "<Run|MPos:-340.500,145.200,-4.500|FS:12000.0,0>"
        self.driver._parse_status_line(raw)
        telem = self.driver.get_telemetry()
        self.assertEqual(telem["state"], "Run")
        self.assertEqual(telem["mpos"], [-340.5, 145.2, -4.5])
        self.assertEqual(telem["feed_rate"], 12000.0)


if __name__ == "__main__":
    unittest.main()
