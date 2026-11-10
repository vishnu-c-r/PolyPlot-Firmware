#!/usr/bin/env python3
import io
import json
import os
import queue
import sys
import threading
import time
import unittest

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

from gateway.gateway_server import GatewayManager, get_closest_corner
from gateway.serial_driver import SerialDriver


class MockSerialDevice:
    def __init__(self):
        self.in_queue = queue.Queue()
        self.written = []
        self.closed = False

    def write(self, data):
        self.written.append(data)
        # Handle command responses
        text = data.decode("utf-8", errors="ignore")
        if "$I" in text:
            self.in_queue.put(b"[VER:4 PLOTCEI v4.0 (main):]\r\n[MSG:Machine: Water_plot]\r\nok\r\n")
        elif "$G" in text:
            self.in_queue.put(b"[GC:G0 G54 G17 G21 G90 G94 M9 T0 F12000]\r\nok\r\n")
        elif "$#" in text:
            self.in_queue.put(b"[G54:0.000,0.000,0.000]\r\nok\r\n")
        elif "?" in text:
            self.in_queue.put(b"<Idle|MPos:-350.000,150.000,-0.500|FS:0.000>\r\n")
        else:
            self.in_queue.put(b"ok\r\n")

    def readline(self):
        try:
            return self.in_queue.get(timeout=0.2)
        except queue.Empty:
            return b""

    def read_all(self):
        res = bytearray()
        while not self.in_queue.empty():
            res.extend(self.in_queue.get_nowait())
        return bytes(res)

    def reset_input_buffer(self):
        while not self.in_queue.empty():
            self.in_queue.get_nowait()

    def close(self):
        self.closed = True


class TestGatewayServer(unittest.TestCase):
    def setUp(self):
        self.manager = GatewayManager(serial_port="mock", baud=115200, http_port=9998, ws_port=9999, auto_connect=False)
        self.mock_serial = MockSerialDevice()
        self.manager.driver.connect(mock_stream=self.mock_serial)
        self.manager.driver.mpos = [-350.0, 150.0, -0.5]
        self.manager.driver.state = "Idle"

    def tearDown(self):
        self.manager.driver.disconnect()

    def _call(self, method, path, body=None, headers=None):
        """Simulates WSGI request directly against bottle app."""
        import bottle
        env = {
            "REQUEST_METHOD": method,
            "PATH_INFO": path,
            "QUERY_STRING": "",
            "SERVER_NAME": "localhost",
            "SERVER_PORT": "8080",
            "wsgi.version": (1, 0),
            "wsgi.url_scheme": "http",
            "wsgi.input": io.BytesIO(b""),
            "wsgi.errors": sys.stderr,
            "wsgi.multithread": True,
            "wsgi.multiprocess": False,
            "wsgi.run_once": False,
        }
        if headers:
            for k, v in headers.items():
                header_key = "HTTP_" + k.upper().replace("-", "_")
                if k.lower() == "content-type":
                    env["CONTENT_TYPE"] = v
                else:
                    env[header_key] = v

        if body is not None:
            if isinstance(body, dict):
                raw = json.dumps(body).encode("utf-8")
                env["CONTENT_TYPE"] = "application/json"
            elif isinstance(body, str):
                raw = body.encode("utf-8")
            else:
                raw = body
            env["wsgi.input"] = io.BytesIO(raw)
            env["CONTENT_LENGTH"] = str(len(raw))

        status_code = [200]
        resp_headers = {}

        def start_response(status, response_headers, exc_info=None):
            status_code[0] = int(status.split()[0])
            for h, v in response_headers:
                resp_headers[h.lower()] = v

        resp_iter = self.manager.app(env, start_response)
        output = b"".join(resp_iter).decode("utf-8")
        return status_code[0], resp_headers, output

    def test_status_endpoint(self):
        status, headers, body = self._call("GET", "/api/status")
        self.assertEqual(status, 200)
        data = json.loads(body)
        self.assertTrue(data["connected"])
        self.assertEqual(data["state"], "Idle")
        self.assertEqual(data["mpos"], [-350.0, 150.0, -0.5])
        self.assertIn("access-control-allow-origin", headers)

    def test_diagnostics_endpoint(self):
        status, headers, body = self._call("GET", "/api/diagnostics")
        self.assertEqual(status, 200)
        data = json.loads(body)
        self.assertTrue(data["connected"])
        self.assertIn("travel_limits", data)
        self.assertEqual(data["travel_limits"]["x"], 730.0)
        self.assertIn("modal_state", data)

    def test_job_upload_and_safety_clamping(self):
        unsafe_gcode = "G0 X-350 Y150\nG1 Z-4.50 F4500 ; Fast unsafe Z plunge\nG1 X-300 Y100 F12000"
        payload = {
            "name": "clamped_test.gcode",
            "gcode": unsafe_gcode
        }
        status, headers, body = self._call("POST", "/api/jobs/upload", body=payload)
        self.assertEqual(status, 200)
        data = json.loads(body)
        self.assertTrue(data["success"])
        job_id = data["job_id"]
        self.assertEqual(data["total_lines"], 3)

        # Inspect internal sanitized job
        job = self.manager.jobs[job_id]
        # Line 2 must be clamped to F1000
        self.assertIn("F1000", job["lines"][1])
        self.assertNotIn("F4500", job["lines"][1])

    def test_job_start_and_controls(self):
        payload = {
            "name": "simple.gcode",
            "gcode": "G0 X-350 Y150\nG1 Z-4.50 F1000\nG0 Z-0.50 F1000"
        }
        _, _, upload_body = self._call("POST", "/api/jobs/upload", body=payload)
        job_id = json.loads(upload_body)["job_id"]

        # Start Job
        status, _, body = self._call("POST", "/api/jobs/start", body={"job_id": job_id})
        self.assertEqual(status, 200)
        data = json.loads(body)
        self.assertTrue(data["success"])
        self.assertEqual(data["status"], "streaming")

        # Hold & Resume
        s_hold, _, b_hold = self._call("POST", "/api/control/hold")
        self.assertEqual(s_hold, 200)
        self.assertTrue(json.loads(b_hold)["success"])

        s_res, _, b_res = self._call("POST", "/api/control/resume")
        self.assertEqual(s_res, 200)
        self.assertTrue(json.loads(b_res)["success"])

        # Abort
        s_ab, _, b_ab = self._call("POST", "/api/control/abort")
        self.assertEqual(s_ab, 200)
        self.assertTrue(json.loads(b_ab)["success"])

    def test_closest_corner_parking(self):
        status, _, body = self._call("POST", "/api/control/park")
        self.assertEqual(status, 200)
        data = json.loads(body)
        self.assertTrue(data["success"])
        self.assertIn("park_pos", data)
        # Center (-350, 150) -> Top-Left corner is (-680, 280)
        park_pos = data["park_pos"]
        self.assertIn(tuple(park_pos), [(-680.0, 280.0), (-20.0, 280.0), (-680.0, 20.0), (-20.0, 20.0)])


if __name__ == "__main__":
    unittest.main()
