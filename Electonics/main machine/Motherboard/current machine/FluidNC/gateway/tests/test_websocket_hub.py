#!/usr/bin/env python3
import asyncio
import json
import os
import sys
import threading
import time
import unittest

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

import websockets
from gateway.gateway_server import GatewayManager


class MockDriver:
    def __init__(self):
        self.connected = True
        self.port_name = "/dev/mock"
        self.state = "Idle"
        self.mpos = [-350.0, 150.0, -0.5]
        self.feed_rate = 12000
        self.buffer_free = 15

    def register_telemetry_callback(self, cb):
        self._cb = cb

    def simulate_telemetry(self, telem):
        if hasattr(self, "_cb"):
            self._cb(telem)


class TestWebSocketHub(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.manager = GatewayManager(serial_port="mock", baud=115200, http_port=9996, ws_port=9997, auto_connect=False)
        self.manager.driver = MockDriver()
        self.manager.ws_hub.start()
        # Give asyncio loop thread a moment to bind port
        await asyncio.sleep(0.3)

    async def asyncTearDown(self):
        self.manager.ws_hub.stop()
        if self.manager.ws_hub.server_obj:
            self.manager.ws_hub.server_obj.close()
        await asyncio.sleep(0.1)

    async def test_websocket_telemetry_and_actions(self):
        uri = "ws://127.0.0.1:9997"
        async with websockets.connect(uri) as ws:
            # 1. First message on connect should be telemetry
            msg_raw = await asyncio.wait_for(ws.recv(), timeout=2.0)
            msg = json.loads(msg_raw)
            self.assertEqual(msg["type"], "telemetry")
            self.assertEqual(msg["data"]["state"], "Idle")
            self.assertEqual(msg["data"]["mpos"], [-350.0, 150.0, -0.5])

            # 2. Simulate driver telemetry broadcast
            self.manager._on_driver_telemetry({
                "state": "Run",
                "mpos": [-300.0, 100.0, -4.5],
                "feed_rate": 9000,
                "buffer_free": 12
            })
            telem_raw = await asyncio.wait_for(ws.recv(), timeout=2.0)
            telem = json.loads(telem_raw)
            self.assertEqual(telem["type"], "telemetry")
            self.assertEqual(telem["data"]["state"], "Run")
            self.assertEqual(telem["data"]["mpos"], [-300.0, 100.0, -4.5])

            # 3. Simulate stream engine progress broadcast
            self.manager._on_stream_progress({
                "job_id": "test_job_1",
                "current_line": 50,
                "total_lines": 100,
                "percent": 50.0,
                "active_command": "G1 X-320 Y120"
            })
            prog_raw = await asyncio.wait_for(ws.recv(), timeout=2.0)
            prog = json.loads(prog_raw)
            self.assertEqual(prog["type"], "job_progress")
            self.assertEqual(prog["data"]["percent"], 50.0)


if __name__ == "__main__":
    unittest.main()

