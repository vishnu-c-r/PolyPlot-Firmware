#!/usr/bin/env python3
"""
Spirit Gateway - Module 3: REST API & WebSocket Streaming Server
================================================================
Headless gateway bridge connecting Next.js frontend to FluidNC over USB.

Features:
- REST API (Bottle on port 8080):
    - POST /api/jobs/upload      (Multipart file or JSON)
    - POST /api/jobs/start       (Dispatches stream)
    - GET  /api/jobs             (List uploaded jobs)
    - POST /api/control/hold     (Immediate '!')
    - POST /api/control/resume   (Immediate '~')
    - POST /api/control/abort    (Immediate 0x18 reset)
    - POST /api/control/jog      (Safe $J jog moves, clamped Z <= 1000)
    - POST /api/control/park     (Closest safe corner park)
    - POST /api/command          (Raw G-code / Grbl query)
    - GET  /api/status           (Real-time telemetry + job progress snapshot)
    - GET  /api/diagnostics      (Structured machine limits, settings, firmware)
- WebSocket Server (websockets on port 8081):
    - Streams telemetry every 200ms
    - Emits job_progress on every line 'ok'
    - Emits job_complete on finish
    - Accepts incoming realtime actions (hold/resume/abort)
- Full CORS support for Next.js localhost / remote dev servers
- Threaded WSGI server to eliminate blocking
"""

import argparse
import asyncio
import datetime
import json
import logging
import math
import os
import re
import socket
from socketserver import ThreadingMixIn
import sys
import threading
import time
from wsgiref.simple_server import WSGIServer, make_server

# Add repo root to path
REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

import bottle

try:
    import websockets
    HAVE_WEBSOCKETS = True
except ImportError:
    HAVE_WEBSOCKETS = False

from gateway.serial_driver import SerialDriver
from gateway.stream_engine import StreamEngine

try:
    from clock_ui_server import generate_clock_gcode_and_paths, DEFAULT_QUOTES
except ImportError:
    generate_clock_gcode_and_paths = None
    DEFAULT_QUOTES = []

logging.basicConfig(
    level=logging.INFO,
    format="[%(asctime)s] [%(levelname)s] [%(name)s] %(message)s",
    datefmt="%H:%M:%S"
)
logger = logging.getLogger("spirit.gateway")

# Safe Hardware Corners for closest-corner parking
SAFE_CORNERS = [
    (-680.0, 280.0),  # Top-Left
    (-20.0, 280.0),   # Top-Right
    (-680.0, 20.0),   # Bottom-Left
    (-20.0, 20.0),    # Bottom-Right
]

def get_closest_corner(x, y):
    """Returns closest corner tuple (cx, cy) to given coordinate."""
    return min(SAFE_CORNERS, key=lambda c: (c[0] - x) ** 2 + (c[1] - y) ** 2)


# ---------------------------------------------------------------------------
# Threaded WSGI Server Adapter for Bottle
# ---------------------------------------------------------------------------
class ThreadingWSGIServer(ThreadingMixIn, WSGIServer):
    daemon_threads = True

class ThreadedWSGIAdapter(bottle.ServerAdapter):
    def run(self, handler):
        self.server = make_server(self.host, self.port, handler, server_class=ThreadingWSGIServer)
        logger.info(f"Threaded HTTP REST Server listening on http://{self.host}:{self.port}")
        self.server.serve_forever()


# ---------------------------------------------------------------------------
# WebSocket Hub
# ---------------------------------------------------------------------------
class WebSocketHub:
    def __init__(self, host="0.0.0.0", port=8081, gateway=None):
        self.host = host
        self.port = port
        self.gateway = gateway
        self.clients = set()
        self.loop = None
        self.thread = None
        self.server_obj = None

    def start(self):
        if not HAVE_WEBSOCKETS:
            logger.warning("websockets package not available; WebSocket server disabled.")
            return
        self.thread = threading.Thread(target=self._run_loop, daemon=True)
        self.thread.start()
        logger.info(f"WebSocket Hub starting on ws://{self.host}:{self.port}")

    def _run_loop(self):
        self.loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self.loop)

        async def handler(websocket):
            self.clients.add(websocket)
            client_ip = websocket.remote_address[0] if websocket.remote_address else "unknown"
            logger.info(f"WebSocket client connected from {client_ip} (Total: {len(self.clients)})")
            try:
                # Send immediate state on connect
                if self.gateway:
                    snap = self.gateway.get_status_snapshot()
                    await websocket.send(json.dumps({"type": "telemetry", "data": snap}))

                async for raw_msg in websocket:
                    self._on_client_message(websocket, raw_msg)
            except Exception as e:
                logger.debug(f"WebSocket error: {e}")
            finally:
                self.clients.discard(websocket)
                logger.info(f"WebSocket client disconnected (Remaining: {len(self.clients)})")

        async def telemetry_ticker():
            while True:
                await asyncio.sleep(0.20)
                if not self.clients or not self.gateway:
                    continue
                snap = self.gateway.get_status_snapshot()
                telem_data = {
                    "state": snap.get("state", "Disconnected"),
                    "mpos": snap.get("mpos", [-350.0, 150.0, -0.5]),
                    "feed": snap.get("feed", 0.0),
                    "buffer_free": snap.get("buffer_free", 15)
                }
                msg_str = json.dumps({"type": "telemetry", "data": telem_data})
                for ws in list(self.clients):
                    try:
                        await ws.send(msg_str)
                    except Exception:
                        self.clients.discard(ws)

        async def main():
            # In websockets 14+, serve accepts handler directly
            try:
                async with websockets.serve(handler, self.host, self.port) as server:
                    self.server_obj = server
                    asyncio.create_task(telemetry_ticker())
                    await asyncio.Future()  # run forever
            except Exception as e:
                logger.error(f"WebSocket server error: {e}")

        try:
            self.loop.run_until_complete(main())
        except Exception as e:
            logger.error(f"WebSocket event loop exited: {e}")

    def _on_client_message(self, websocket, raw_msg):
        """Dispatches incoming client action commands over WebSocket."""
        try:
            msg = json.loads(raw_msg)
        except Exception:
            return

        action = msg.get("action") or msg.get("type")
        if not action or not self.gateway:
            return

        action = action.lower()
        if action == "hold":
            self.gateway.hold()
        elif action == "resume":
            self.gateway.resume()
        elif action == "abort":
            self.gateway.abort()
        elif action == "jog":
            x = float(msg.get("x", 0.0))
            y = float(msg.get("y", 0.0))
            z = float(msg.get("z", 0.0))
            f = int(msg.get("feed", 10000))
            self.gateway.jog(x, y, z, f)
        elif action == "command":
            cmd = msg.get("command", "")
            if cmd:
                self.gateway.send_command(cmd)

    def broadcast(self, payload):
        """Thread-safe broadcast to all connected WebSocket clients."""
        if not self.loop or not self.clients:
            return
        data_str = json.dumps(payload)
        for ws in list(self.clients):
            try:
                asyncio.run_coroutine_threadsafe(ws.send(data_str), self.loop)
            except Exception:
                self.clients.discard(ws)


# ---------------------------------------------------------------------------
# Gateway Manager
# ---------------------------------------------------------------------------
class GatewayManager:
    def __init__(self, serial_port="auto", baud=115200, http_port=8080, ws_port=8081, auto_connect=True):
        self.lock = threading.RLock()
        self.serial_port = serial_port
        self.baud = baud
        self.http_port = http_port
        self.ws_port = ws_port

        # Jobs Cache
        self.jobs = {}  # job_id -> job_dict
        self.latest_job_id = None

        # Instantiate Driver and Stream Engine
        self.driver = SerialDriver(port=serial_port, baud=baud)
        self.engine = StreamEngine(self.driver)

        # Wire Callbacks
        self.driver.register_telemetry_callback(self._on_driver_telemetry)
        self.engine.register_progress_callback(self._on_stream_progress)
        self.engine.register_complete_callback(self._on_stream_complete)
        self.engine.register_error_callback(self._on_stream_error)

        # Instantiate WebSocket Hub
        self.ws_hub = WebSocketHub(host="0.0.0.0", port=ws_port, gateway=self)

        # Bottle App
        self.app = bottle.Bottle()
        self._setup_routes()

        # Autonomous Clock State (for backward-compatibility with clock.html)
        self.clock_active = False
        self.clock_config = {
            "style": "quote",
            "center_x": -350.0,
            "center_y": 150.0,
            "radius": 110.0,
            "z_up": -0.5,
            "z_down": -4.5,
            "feed_xy": 10000,
            "feed_z": 1000,
            "rapid_xy": 10000,
            "jog_feed_xy": 10000,
            "jog_feed_z": 1000,
            "reset_wco": True,
            "mirror_x": True,
            "quote_text": "TIME FLOWS LIKE WATER",
            "quote_rotate": True
        }
        self.last_clock_drawn_min = -1

        # Preload presaved G-code art jobs into cache
        self._preload_presaved_jobs()

        # Connect driver & start clock loop in background
        if auto_connect:
            threading.Thread(target=self._serial_monitor_loop, daemon=True).start()
            threading.Thread(target=self._clock_scheduler_loop, daemon=True).start()

    def _clock_scheduler_loop(self):
        while True:
            time.sleep(0.1)
            if not self.clock_active or not self.driver.connected or self.engine.is_streaming:
                continue
            now = datetime.datetime.now()
            if now.second == 0 and now.minute != self.last_clock_drawn_min and now.microsecond < 300_000:
                self.last_clock_drawn_min = now.minute
                self.trigger_clock_draw(now)

    def trigger_clock_draw(self, dt=None):
        if dt is None:
            dt = datetime.datetime.now()
        if not generate_clock_gcode_and_paths:
            return False, "Clock generator not available"
        lines, _, _, _ = generate_clock_gcode_and_paths(dt, self.clock_config)
        job = StreamEngine.prepare_job(lines, job_name=f"Clock {dt.strftime('%H:%M')}")
        self.jobs[job["job_id"]] = job
        self.latest_job_id = job["job_id"]
        return self.engine.start_job(job)

    def _serial_monitor_loop(self):
        """Continuously monitors serial connection and auto-reconnects when available."""
        time.sleep(0.5)
        while True:
            if not self.driver.connected:
                target = self.serial_port
                if target.lower() == "auto" or not os.path.exists(target):
                    ports = SerialDriver.list_available_ports()
                    new_target = None
                    for p in ports:
                        desc = (p.get("description") or "").lower()
                        dev = (p.get("device") or "").lower()
                        if any(k in desc or k in dev for k in ("ch340", "cp210", "esp32", "ttyusb", "ttyacm")):
                            new_target = p["device"]
                            break
                    if not new_target and ports:
                        new_target = ports[0]["device"]
                    if new_target:
                        target = new_target

                if target and target.lower() != "auto" and (os.path.exists(target) or target.startswith("COM")):
                    self.driver.port_name = target
                    ok, msg = self.driver.connect()
                    if ok:
                        logger.info(f"Driver connected to {self.driver.port_name} at {self.baud} baud")
                    else:
                        logger.debug(f"Connection attempt to {self.driver.port_name} failed: {msg}")
            time.sleep(2.0)

    # Callback Handlers
    def _on_driver_telemetry(self, telem):
        payload = {
            "type": "telemetry",
            "data": {
                "state": telem.get("state", "Disconnected"),
                "mpos": telem.get("mpos", [-350.0, 150.0, -0.5]),
                "feed": telem.get("feed_rate", 0),
                "buffer_free": telem.get("buffer_free", 15)
            }
        }
        self.ws_hub.broadcast(payload)

    def _on_stream_progress(self, prog):
        payload = {
            "type": "job_progress",
            "data": prog
        }
        self.ws_hub.broadcast(payload)

    def _on_stream_complete(self, job_dict):
        payload = {
            "type": "job_complete",
            "data": {
                "job_id": job_dict.get("job_id"),
                "status": "success",
                "total_lines": job_dict.get("total_lines", 0)
            }
        }
        self.ws_hub.broadcast(payload)

    def _on_stream_error(self, job_dict, err_msg):
        payload = {
            "type": "job_error",
            "data": {
                "job_id": job_dict.get("job_id") if job_dict else "unknown",
                "error": err_msg
            }
        }
        self.ws_hub.broadcast(payload)

    # High-level controls
    def hold(self):
        return self.engine.pause()

    def resume(self):
        return self.engine.resume()

    def abort(self):
        return self.engine.abort()

    def jog(self, dx=0.0, dy=0.0, dz=0.0, feed=10000):
        if not self.driver.connected:
            return False, "Machine not connected"
        feed_z = min(feed, 1000)
        parts = ["$J=G91 G21"]
        if dx != 0.0:
            parts.append(f"X{dx:.3f}")
        if dy != 0.0:
            parts.append(f"Y{dy:.3f}")
        if dz != 0.0:
            parts.append(f"Z{dz:.3f}")
            actual_feed = feed_z
        else:
            actual_feed = feed
        parts.append(f"F{actual_feed}")
        cmd = " ".join(parts)
        return self.driver.send_line(cmd)

    def park(self):
        """Lifts pen safely and rapids to closest safe corner."""
        if not self.driver.connected:
            return False, "Machine not connected"
        curr_x, curr_y = self.driver.mpos[0], self.driver.mpos[1]
        park_x, park_y = get_closest_corner(curr_x, curr_y)
        self.driver.send_line("G0 Z-0.50 F1000")
        self.driver.send_line(f"G0 X{park_x:.3f} Y{park_y:.3f} F10000")
        return True, (park_x, park_y)

    def unlock(self):
        """Sends $X to clear alarm state."""
        if not self.driver.connected:
            return False, "Machine not connected"
        ok, resp = self.driver.send_line("$X")
        self.driver.send_realtime(b"?")
        return ok, resp

    def home(self):
        """Sends $H to execute homing cycle asynchronously."""
        if not self.driver.connected:
            return False, "Machine not connected"
        def _do_home():
            self.driver.send_line("$H", timeout=60.0)
            self.driver.send_realtime(b"?")
        threading.Thread(target=_do_home, daemon=True).start()
        return True, "Homing cycle started"

    def send_command(self, cmd):
        clean = cmd.strip()
        if not clean:
            return False, "Empty command"
        if clean in ("?", "!", "~") or (len(clean) == 1 and ord(clean) < 32):
            ok = self.driver.send_realtime(clean)
            return ok, "realtime sent" if ok else "send failed"
        return self.driver.send_line(clean)

    def get_status_snapshot(self):
        now = datetime.datetime.now()
        sec_to_next = 60.0 - now.second - (now.microsecond / 1_000_000.0)
        time_since_draw = now.second + (now.microsecond / 1_000_000.0)
        evap_pct = max(0.0, min(100.0, (time_since_draw / 60.0) * 100.0))

        snap = {
            "connected": self.driver.connected,
            "port": self.driver.port_name,
            "state": self.driver.state,
            "machine_state": self.driver.state,
            "mpos": self.driver.mpos,
            "feed": self.driver.feed_rate,
            "buffer_free": getattr(self.driver, "buffer_free", 15),
            "streaming": self.engine.is_streaming,
            "paused": self.engine.is_paused,
            "clock_active": self.clock_active,
            "current_job_active": self.engine.is_streaming,
            "current_job_progress": round(self.engine.current_job.get("percent", 0.0), 1) if self.engine.current_job else 0.0,
            "current_job_line": self.engine.current_job.get("active_command", "") if self.engine.current_job else "",
            "next_draw_sec": round(sec_to_next, 1),
            "evap_pct": round(evap_pct, 1),
            "current_time_str": now.strftime("%H:%M:%S"),
            "config": self.clock_config,
            "job": None
        }
        if self.engine.current_job:
            j = self.engine.current_job
            snap["job"] = {
                "job_id": j.get("job_id"),
                "name": j.get("name"),
                "total_lines": j.get("total_lines", 0),
                "current_line": j.get("current_line", 0),
                "percent": j.get("percent", 0.0),
                "active_command": j.get("active_command", ""),
                "status": j.get("status", "unknown")
            }
        return snap

    def get_structured_diagnostics(self):
        """Queries machine controller over serial driver for complete diagnostics."""
        diag = {
            "connected": self.driver.connected,
            "port": self.driver.port_name,
            "baud": self.driver.baud,
            "state": self.driver.state,
            "mpos": self.driver.mpos,
            "firmware": "FluidNC / Grbl ESP32",
            "machine_name": "Water_plot",
            "travel_limits": {"x": 730.0, "y": 300.0, "z": 10.0},
            "max_feedrates": {"x": 15000.0, "y": 15000.0, "z": 2500.0},
            "accelerations": {"x": 1500.0, "y": 1500.0, "z": 600.0},
            "steps_per_mm": {"x": 200.0, "y": 200.0, "z": 200.0},
            "modal_state": "G0 G54 G17 G21 G90 G94 M9 T0 F10000",
            "soft_limits": True,
            "hard_limits": False,
            "homing_enabled": True
        }
        if self.driver.connected:
            # Query $I
            ok, resp_i = self.driver.send_line("$I")
            if ok:
                diag["raw_build_info"] = resp_i.splitlines()
                v_match = re.search(r"\[VER:([^\]]+)\]", resp_i)
                if v_match:
                    diag["firmware"] = v_match.group(1)
                m_match = re.search(r"\[MSG:Machine:\s*([^\]]+)\]", resp_i)
                if m_match:
                    diag["machine_name"] = m_match.group(1).strip()
                ip_match = re.search(r"IP=([\d\.]+)", resp_i)
                if ip_match:
                    diag["controller_wifi_ip"] = ip_match.group(1)

            # Query $G
            ok, resp_g = self.driver.send_line("$G")
            if ok:
                gc_match = re.search(r"\[GC:([^\]]+)\]", resp_g)
                if gc_match:
                    diag["modal_state"] = gc_match.group(1).strip()

            # Query $#
            ok, resp_hash = self.driver.send_line("$#")
            if ok:
                diag["raw_offsets"] = [l.strip() for l in resp_hash.splitlines() if l.startswith("[")]
        return diag

    # -----------------------------------------------------------------------
    # REST API Route Declarations
    # -----------------------------------------------------------------------
    def _setup_routes(self):
        app = self.app

        # CORS Hook
        @app.hook("after_request")
        def enable_cors():
            bottle.response.headers["Access-Control-Allow-Origin"] = "*"
            bottle.response.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS, PUT, DELETE"
            bottle.response.headers["Access-Control-Allow-Headers"] = "Origin, Accept, Content-Type, Authorization, X-Requested-With"

        @app.route("/<:re:.*>", method="OPTIONS")
        def handle_options():
            return {}

        # 1. Real-time Status
        @app.route("/api/status")
        def api_status():
            bottle.response.content_type = "application/json"
            return self.get_status_snapshot()

        # 2. Machine Diagnostics
        @app.route("/api/diagnostics")
        def api_diagnostics():
            bottle.response.content_type = "application/json"
            return self.get_structured_diagnostics()

        # 3. Available Serial Ports
        @app.route("/api/ports")
        def api_ports():
            bottle.response.content_type = "application/json"
            return {"ports": SerialDriver.list_available_ports()}

        # 4. Job Upload (Multipart File or JSON)
        @app.route("/api/jobs/upload", method="POST")
        def api_job_upload():
            bottle.response.content_type = "application/json"
            content_type = bottle.request.headers.get("Content-Type", "")

            gcode_text = ""
            job_name = "Upload"

            # Check multipart file
            upload = bottle.request.files.get("file")
            if upload:
                job_name = upload.filename
                gcode_text = upload.file.read().decode("utf-8", errors="ignore")
            elif "application/json" in content_type:
                data = bottle.request.json or {}
                job_name = data.get("name", "WebJob")
                gcode_text = data.get("gcode", "")
            else:
                # Raw text body
                gcode_text = bottle.request.body.read().decode("utf-8", errors="ignore")
                job_name = "TextStream"

            if not gcode_text.strip():
                bottle.response.status = 400
                return {"success": False, "error": "No G-code content received"}

            # Prepare & sanitize job (Clamps Z feedrate <= 1000)
            job = StreamEngine.prepare_job(gcode_text, job_name=job_name)
            self.jobs[job["job_id"]] = job
            self.latest_job_id = job["job_id"]

            logger.info(f"Loaded job '{job_name}' ({job['job_id']}) with {job['total_lines']} clean lines")
            return {
                "success": True,
                "job_id": job["job_id"],
                "name": job["name"],
                "total_lines": job["total_lines"],
                "lines_count": job["total_lines"],
                "status": "ready"
            }

        # 5. Job Start
        @app.route("/api/jobs/start", method="POST")
        def api_job_start():
            bottle.response.content_type = "application/json"
            data = bottle.request.json or {}
            job_id = data.get("job_id") or self.latest_job_id
            if not job_id or job_id not in self.jobs:
                bottle.response.status = 404
                return {"success": False, "error": f"Job {job_id} not found"}

            job = self.jobs[job_id]
            ok, msg = self.engine.start_job(job)
            if not ok:
                bottle.response.status = 400
                return {"success": False, "error": msg}

            return {"success": True, "status": "streaming", "job_id": job_id}

        # 6. List Jobs
        @app.route("/api/jobs")
        def api_jobs_list():
            bottle.response.content_type = "application/json"
            res = []
            for jid, j in self.jobs.items():
                res.append({
                    "job_id": jid,
                    "name": j.get("name"),
                    "total_lines": j.get("total_lines", 0),
                    "status": j.get("status", "ready")
                })
            return {"jobs": res}

        # 6b. Direct Plot from Vector Generators
        @app.route("/api/plot", method="POST")
        def api_plot():
            bottle.response.content_type = "application/json"
            if not self.driver.connected:
                bottle.response.status = 503
                return {"success": False, "error": "FluidNC controller not connected"}

            data = bottle.request.json or {}
            lines = data.get("lines", [])
            title = data.get("title", "Custom Art Plot")
            if not lines:
                bottle.response.status = 400
                return {"success": False, "error": "No G-code lines provided"}

            job = StreamEngine.prepare_job(lines, job_name=title)
            self.jobs[job["job_id"]] = job
            self.latest_job_id = job["job_id"]

            ok, msg = self.engine.start_job(job)
            if not ok:
                bottle.response.status = 400
                return {"success": False, "error": msg}

            logger.info(f"Started direct plot job '{title}' ({job['job_id']}) with {job['total_lines']} lines")
            return {
                "success": True,
                "message": msg,
                "job_id": job["job_id"],
                "total_lines": job["total_lines"],
                "status": "streaming"
            }

        # 7. Hardware Motion Controls
        @app.route("/api/control/hold", method="POST")
        def api_control_hold():
            bottle.response.content_type = "application/json"
            ok = self.hold()
            return {"success": ok, "status": "held" if ok else "error"}

        @app.route("/api/control/resume", method="POST")
        def api_control_resume():
            bottle.response.content_type = "application/json"
            ok = self.resume()
            return {"success": ok, "status": "running" if ok else "error"}

        @app.route("/api/control/abort", method="POST")
        def api_control_abort():
            bottle.response.content_type = "application/json"
            ok = self.abort()
            return {"success": ok, "status": "aborted" if ok else "error"}

        @app.route("/api/control/jog", method="POST")
        def api_control_jog():
            bottle.response.content_type = "application/json"
            data = bottle.request.json or {}
            dx = float(data.get("x", 0.0))
            dy = float(data.get("y", 0.0))
            dz = float(data.get("z", 0.0))
            feed = int(data.get("feed", 10000))
            ok, msg = self.jog(dx, dy, dz, feed)
            return {"success": ok, "status": "jogging" if ok else "error", "message": msg}

        @app.route("/api/control/park", method="POST")
        def api_control_park():
            bottle.response.content_type = "application/json"
            ok, res = self.park()
            if ok:
                return {"success": True, "park_pos": list(res)}
            return {"success": False, "error": res}

        @app.route("/api/control/unlock", method="POST")
        def api_control_unlock():
            bottle.response.content_type = "application/json"
            ok, msg = self.unlock()
            return {"success": ok, "status": "unlocked" if ok else "error", "message": msg}

        @app.route("/api/control/home", method="POST")
        def api_control_home():
            bottle.response.content_type = "application/json"
            ok, msg = self.home()
            return {"success": ok, "status": "homing" if ok else "error", "message": msg}

        # 8. Raw Command Dispatch
        @app.route("/api/command", method="POST")
        def api_command():
            bottle.response.content_type = "application/json"
            data = bottle.request.json or {}
            cmd = data.get("command", "")
            ok, resp = self.send_command(cmd)
            return {"success": ok, "response": resp}

        # 9. Clock Endpoints (Autonomous Water Clock)
        @app.route("/api/clock/toggle", method="POST")
        def api_clock_toggle():
            bottle.response.content_type = "application/json"
            self.clock_active = not self.clock_active
            logger.info(f"Clock mode {'ACTIVATED' if self.clock_active else 'PAUSED'}")
            return {"clock_active": self.clock_active}

        @app.route("/api/clock/trigger", method="POST")
        def api_clock_trigger():
            bottle.response.content_type = "application/json"
            if not self.driver.connected:
                bottle.response.status = 400
                return {"success": False, "error": "Machine not connected"}
            ok, msg = self.trigger_clock_draw()
            return {"success": ok, "message": msg}

        @app.route("/api/clock/config", method=["POST", "GET"])
        def api_clock_config():
            bottle.response.content_type = "application/json"
            if bottle.request.method == "POST":
                data = bottle.request.json or {}
                self.clock_config.update(data)
            return {"success": True, "config": self.clock_config}

        @app.route("/api/preview")
        def api_preview():
            bottle.response.content_type = "application/json"
            if not generate_clock_gcode_and_paths:
                return {"time": "00:00", "strokes": [], "total_dist_mm": 0, "est_sec": 0}
            now = datetime.datetime.now()
            lines, strokes, dist, est = generate_clock_gcode_and_paths(now, self.clock_config)
            return {
                "time": now.strftime("%H:%M"),
                "strokes": strokes,
                "total_dist_mm": round(dist, 1),
                "est_sec": round(est, 1),
                "gcode_sample": lines[:40]
            }

        # 10. Static UI fallback (serves clock.html or index)
        @app.route("/")
        @app.route("/clock.html")
        @app.route("/index.html")
        def serve_ui():
            script_dir = os.path.abspath(os.path.join(REPO_ROOT))
            clock_path = os.path.join(script_dir, "clock.html")
            if os.path.exists(clock_path):
                bottle.response.set_header("Cache-Control", "no-cache, no-store, must-revalidate")
                return bottle.static_file("clock.html", root=script_dir)
            index_path = os.path.join(script_dir, "index.html")
            if os.path.exists(index_path):
                bottle.response.set_header("Cache-Control", "no-cache, no-store, must-revalidate")
                return bottle.static_file("index.html", root=script_dir)
            return "<h3>Spirit Gateway Active</h3><p>Use REST API or connect WebSocket on :8081</p>"


    def _preload_presaved_jobs(self):
        """Scans disk for presaved art/jobs and loads them into self.jobs."""
        search_dirs = [
            os.path.join(REPO_ROOT, "jobs"),
            "/opt/spirit/jobs",
            REPO_ROOT,
        ]
        friendly_names = {
            "polyplot_woman_and_dog.gcode": "Woman & Dog (One-Line Art)",
            "polyplot_mandala_lotus.gcode": "Lotus Mandala (Parametric)",
            "test_art_face.gcode": "Picasso Face (1 Line)",
            "test_art_rose.gcode": "Rose Blossom (1 Line)",
            "test_dog.gcode": "Curled Dog (Continuous Line)",
            "test_cursive_sentence.gcode": "Continuous Cursive Sentence",
            "test_cursive_word.gcode": "Continuous Cursive Word",
            "polyplot_clock_circle.gcode": "Water Clock Circle",
            "polyplot_clock_square.gcode": "Water Clock Square",
            "test_from_svg.gcode": "Imported SVG Vector Test"
        }
        loaded = 0
        seen_names = set()
        for sdir in search_dirs:
            if not os.path.exists(sdir):
                continue
            for fname in os.listdir(sdir):
                if not fname.endswith(".gcode"):
                    continue
                if fname in seen_names:
                    continue
                if fname in friendly_names or sdir.endswith("jobs") or fname.startswith("polyplot_") or fname.startswith("test_art_"):
                    fpath = os.path.join(sdir, fname)
                    try:
                        with open(fpath, "r", encoding="utf-8", errors="ignore") as f:
                            content = f.read()
                        jname = friendly_names.get(fname, fname.replace(".gcode", "").replace("_", " ").title())
                        job = StreamEngine.prepare_job(content, job_name=jname)
                        self.jobs[job["job_id"]] = job
                        seen_names.add(fname)
                        loaded += 1
                    except Exception as e:
                        logger.warning(f"Could not preload {fname}: {e}")
        logger.info(f"Preloaded {loaded} presaved art jobs into gateway cache")


# ---------------------------------------------------------------------------
# Server Entrypoint
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="Spirit REST API & WebSocket Streaming Gateway")
    parser.add_argument("--serial", type=str, default="auto", help="Serial port (e.g. /dev/ttyUSB0, COM14, or auto)")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate (default: 115200)")
    parser.add_argument("--host", type=str, default="0.0.0.0", help="HTTP & WS listen host (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=8080, help="HTTP REST port (default: 8080)")
    parser.add_argument("--ws-port", type=int, default=8081, help="WebSocket port (default: 8081)")
    args = parser.parse_args()

    print("=" * 70)
    print("      Spirit Modular Gateway Server (REST API + WebSocket)")
    print("=" * 70)
    print(f" REST API:   http://{args.host}:{args.port}/api/status")
    print(f" WebSockets: ws://{args.host}:{args.ws_port}/")
    print(f" Serial:     {args.serial} @ {args.baud} baud")
    print("=" * 70)

    manager = GatewayManager(
        serial_port=args.serial,
        baud=args.baud,
        http_port=args.port,
        ws_port=args.ws_port
    )

    # Start WebSocket Hub
    manager.ws_hub.start()

    # Start Threaded HTTP Server (Blocks main thread)
    adapter = ThreadedWSGIAdapter(host=args.host, port=args.port)
    bottle.run(manager.app, server=adapter, quiet=True)


if __name__ == "__main__":
    main()
