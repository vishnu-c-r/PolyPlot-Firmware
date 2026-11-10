#!/usr/bin/env python3
"""
Spirit Gateway - Module 1: Serial Driver
=========================================
Thread-safe serial driver for FluidNC / Grbl controllers over USB.
Handles:
- DTR/RTS hardware reset mitigation.
- Normal G-code line send/response (Ping-Pong).
- Real-time out-of-band commands (?, !, ~, 0x18).
- Continuous background status polling & telemetry parsing.
"""

import logging
import re
import threading
import time

try:
    import serial
    HAVE_SERIAL = True
except ImportError:
    HAVE_SERIAL = False

logger = logging.getLogger("spirit.serial_driver")


class SerialDriver:
    # Grbl status regex: <Idle|MPos:-350.000,150.000,-0.500|FS:0.0,0|Ov:100,100,100>
    STATUS_PATTERN = re.compile(
        r"<([A-Za-z]+)\|MPos:([-\d\.]+),([-\d\.]+),([-\d\.]+)(?:\|FS:([-\d\.]+),([-\d\.]+))?"
    )

    def __init__(self, port="/dev/ttyUSB0", baud=115200, poll_interval=0.20):
        self.port_name = port
        self.baud = baud
        self.poll_interval = poll_interval

        self.serial_conn = None
        self.connected = False
        self.lock = threading.RLock()

        # Telemetry State
        self.state = "Disconnected"  # Idle, Run, Hold, Alarm, Disconnected
        self.mpos = [0.0, 0.0, 0.0]
        self.feed_rate = 0.0
        self.spindle_speed = 0.0
        self.buffer_free = 15
        self.last_seen = 0.0

        # Callbacks
        self._telemetry_callbacks = []

        # Background Threads
        self._stop_event = threading.Event()
        self._poller_thread = None

    @staticmethod
    def list_available_ports():
        """Lists available serial ports on the system."""
        if not HAVE_SERIAL:
            return []
        try:
            import serial.tools.list_ports
            return [
                {
                    "device": p.device,
                    "description": p.description,
                    "hwid": p.hwid
                }
                for p in serial.tools.list_ports.comports()
            ]
        except Exception:
            return []

    def register_telemetry_callback(self, cb):
        """Registers a callback fn(telemetry_dict) called on new status packets."""
        if cb not in self._telemetry_callbacks:
            self._telemetry_callbacks.append(cb)

    def connect(self, mock_stream=None):
        """
        Opens serial port. If mock_stream is provided, uses it instead of pyserial.
        Mitigates DTR reset by waiting 1.8 seconds.
        """
        with self.lock:
            if self.connected:
                self.disconnect()

            if mock_stream is not None:
                self.serial_conn = mock_stream
                self.connected = True
                self.state = "Idle"
                self.last_seen = time.time()
                self._start_poller()
                return True, "Connected to mock serial"

            if not HAVE_SERIAL:
                return False, "pyserial not installed"

            try:
                logger.info(f"Opening {self.port_name} at {self.baud} baud...")
                # Note: dtr=False, rts=False helps reduce ESP32 auto-reset
                ser = serial.Serial(
                    port=self.port_name,
                    baudrate=self.baud,
                    timeout=0.25,
                    write_timeout=1.0,
                    dsrdtr=False,
                    rtscts=False
                )
                try:
                    ser.dtr = False
                    ser.rts = False
                except Exception:
                    pass

                # Required boot delay for ESP32 bootloader
                time.sleep(1.8)
                ser.reset_input_buffer()

                self.serial_conn = ser
                self.connected = True
                self.state = "Idle"
                self.last_seen = time.time()

                self._stop_event.clear()
                self._start_poller()
                logger.info(f"Connected to {self.port_name}")
                return True, "Connected successfully"
            except Exception as e:
                logger.error(f"Failed to connect to {self.port_name}: {e}")
                self.connected = False
                self.state = "Disconnected"
                return False, str(e)

    def disconnect(self):
        """Stops poller and closes serial port."""
        self._stop_event.set()
        if self._poller_thread and self._poller_thread.is_alive():
            self._poller_thread.join(timeout=1.0)

        with self.lock:
            if self.serial_conn:
                try:
                    self.serial_conn.close()
                except Exception:
                    pass
                self.serial_conn = None
            self.connected = False
            self.state = "Disconnected"
            logger.info("Disconnected from serial port")

    def send_line(self, gcode_line, timeout=6.0):
        """
        Sends a single G-code line and blocks until 'ok' or 'error:N' is returned.
        Returns: (success: bool, response: str)
        """
        clean = gcode_line.strip()
        if not clean or clean.startswith(";"):
            return True, "ok"

        with self.lock:
            if not self.connected or not self.serial_conn:
                return False, "Error: Not connected"

            try:
                self.serial_conn.write((clean + "\n").encode("utf-8"))
                t0 = time.time()
                resp_lines = []

                while time.time() - t0 < timeout:
                    line = self._read_line_decoded()
                    if not line:
                        continue

                    # If status packet arrived interleaved, parse it immediately
                    if line.startswith("<"):
                        self._parse_status_line(line)
                        continue

                    resp_lines.append(line)
                    if line.startswith("ok"):
                        return True, "\n".join(resp_lines)
                    elif line.startswith("error:"):
                        logger.warning(f"FluidNC returned {line} for: {clean}")
                        return False, "\n".join(resp_lines)

                return False, f"Timeout waiting for ok: {clean}"
            except Exception as e:
                logger.error(f"Serial send error: {e}")
                self.disconnect()
                return False, f"Comm Error: {e}"

    def send_realtime(self, char_cmd):
        """
        Sends an out-of-band 1-byte command (?, !, ~, 0x18) immediately.
        Does NOT append newline and does NOT wait for 'ok'.
        """
        with self.lock:
            if not self.connected or not self.serial_conn:
                return False

            try:
                if isinstance(char_cmd, str):
                    data = char_cmd.encode("utf-8")
                elif isinstance(char_cmd, int):
                    data = bytes([char_cmd])
                else:
                    data = bytes(char_cmd)

                self.serial_conn.write(data)
                return True
            except Exception as e:
                logger.error(f"Error sending realtime command: {e}")
                return False

    def _read_line_decoded(self):
        """Reads one stripped line from serial connection."""
        try:
            raw = self.serial_conn.readline()
            if isinstance(raw, str):
                return raw.strip()
            return raw.decode("utf-8", errors="ignore").strip()
        except Exception:
            return ""

    def _start_poller(self):
        self._poller_thread = threading.Thread(target=self._poller_loop, daemon=True)
        self._poller_thread.start()

    def _poller_loop(self):
        """Background loop sending '?' every poll_interval."""
        while not self._stop_event.is_set():
            time.sleep(self.poll_interval)
            if not self.connected:
                continue

            with self.lock:
                if not self.connected or not self.serial_conn:
                    continue
                try:
                    self.serial_conn.write(b"?")
                    time.sleep(0.03)
                    # Read available response
                    if hasattr(self.serial_conn, "read_all"):
                        raw = self.serial_conn.read_all().decode("utf-8", errors="ignore")
                    else:
                        raw = self._read_line_decoded()

                    if raw:
                        for part in raw.split("\n"):
                            part = part.strip()
                            if part.startswith("<"):
                                self._parse_status_line(part)
                except Exception as e:
                    logger.debug(f"Poller exception: {e}")

    def _parse_status_line(self, line):
        """Parses <Idle|MPos:-350.000,150.000,-0.500|FS:0,0> and notifies callbacks."""
        match = self.STATUS_PATTERN.search(line)
        if match:
            self.state = match.group(1)
            self.mpos = [float(match.group(2)), float(match.group(3)), float(match.group(4))]
            if match.group(5):
                self.feed_rate = float(match.group(5))
            if match.group(6):
                self.spindle_speed = float(match.group(6))
            self.last_seen = time.time()

            telemetry = self.get_telemetry()
            for cb in self._telemetry_callbacks:
                try:
                    cb(telemetry)
                except Exception as e:
                    logger.error(f"Telemetry callback error: {e}")

    def get_telemetry(self):
        return {
            "connected": self.connected,
            "port": self.port_name,
            "state": self.state,
            "mpos": list(self.mpos),
            "feed_rate": self.feed_rate,
            "spindle_speed": self.spindle_speed,
            "last_seen": round(self.last_seen, 2)
        }

