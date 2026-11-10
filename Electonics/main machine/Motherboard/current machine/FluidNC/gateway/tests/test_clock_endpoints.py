#!/usr/bin/env python3
import io
import json
import os
import sys
import unittest

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

from gateway.gateway_server import GatewayManager


class MockSerialDevice:
    def __init__(self):
        self.closed = False
    def write(self, data):
        pass
    def readline(self):
        return b"ok\r\n"
    def read_all(self):
        return b""
    def reset_input_buffer(self):
        pass
    def close(self):
        self.closed = True


class TestClockEndpoints(unittest.TestCase):
    def setUp(self):
        self.manager = GatewayManager(serial_port="mock", baud=115200, http_port=9994, ws_port=9995, auto_connect=False)
        self.mock_serial = MockSerialDevice()
        self.manager.driver.connect(mock_stream=self.mock_serial)
        self.manager.driver.mpos = [-350.0, 150.0, -0.5]
        self.manager.driver.state = "Idle"

    def tearDown(self):
        self.manager.driver.disconnect()

    def _call(self, method, path, body=None):
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
        if body is not None:
            raw = json.dumps(body).encode("utf-8")
            env["CONTENT_TYPE"] = "application/json"
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

    def test_clock_toggle(self):
        status, _, body = self._call("POST", "/api/clock/toggle")
        self.assertEqual(status, 200)
        data = json.loads(body)
        self.assertTrue(data["clock_active"])

        status, _, body = self._call("POST", "/api/clock/toggle")
        self.assertEqual(status, 200)
        data = json.loads(body)
        self.assertFalse(data["clock_active"])

    def test_clock_config(self):
        new_cfg = {"style": "quote", "quote_text": "ENERGY FLOWS WITH FOCUS"}
        status, _, body = self._call("POST", "/api/clock/config", body=new_cfg)
        self.assertEqual(status, 200)
        data = json.loads(body)
        self.assertEqual(data["config"]["quote_text"], "ENERGY FLOWS WITH FOCUS")

    def test_clock_preview(self):
        status, _, body = self._call("GET", "/api/preview")
        self.assertEqual(status, 200)
        data = json.loads(body)
        self.assertIn("time", data)
        self.assertIn("strokes", data)
        self.assertGreater(data["total_dist_mm"], 50.0)


if __name__ == "__main__":
    unittest.main()

