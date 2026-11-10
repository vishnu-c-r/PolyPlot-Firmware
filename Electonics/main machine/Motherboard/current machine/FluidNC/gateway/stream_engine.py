#!/usr/bin/env python3
"""
Spirit Gateway - Module 2: Stream Engine
========================================
Streaming engine for line-by-line G-code dispatch over USB serial.
Features:
- Hardware safety clamping: Z feedrate strictly capped at 1000 mm/min (F1000).
- Strips comments and translates M400 -> G4 P0.05.
- Thread-safe Ping-Pong stream loop with pause (!), resume (~), and abort (0x18).
- Progress event emission for real-time WebSocket broadcasting.
"""

import logging
import math
import re
import threading
import time
import uuid

logger = logging.getLogger("spirit.stream_engine")


class StreamEngine:
    MAX_Z_FEED = 1000  # Hard hardware safety limit: 1000 mm/min
    SAFE_PEN_UP_Z = -0.50
    PEN_DOWN_Z = -4.50

    def __init__(self, serial_driver):
        self.driver = serial_driver
        self.lock = threading.RLock()

        # Job State
        self.current_job = None
        self.is_streaming = False
        self.is_paused = False
        self._abort_requested = False

        self._pause_event = threading.Event()
        self._pause_event.set()  # Set means NOT paused

        self._worker_thread = None

        # Event Listeners
        self._progress_callbacks = []
        self._complete_callbacks = []
        self._error_callbacks = []

    def register_progress_callback(self, cb):
        if cb not in self._progress_callbacks:
            self._progress_callbacks.append(cb)

    def register_complete_callback(self, cb):
        if cb not in self._complete_callbacks:
            self._complete_callbacks.append(cb)

    def register_error_callback(self, cb):
        if cb not in self._error_callbacks:
            self._error_callbacks.append(cb)

    # -----------------------------------------------------------------------
    # Safety Interceptor & G-Code Sanitizer
    # -----------------------------------------------------------------------
    @classmethod
    def sanitize_line(cls, line):
        """
        Cleans a single G-code line and enforces hardware safety rules:
        1. Strips comments (;... and (...)).
        2. Clamps any Z move feedrate to <= MAX_Z_FEED (1000 mm/min).
        3. Replaces unsupported M400 with G4 P0.05.
        Returns: sanitized_line (str) or empty string if ignored.
        """
        raw = line.strip()
        if not raw:
            return ""

        # Remove parentheses comments: (comment)
        clean = re.sub(r"\(.*?\)", "", raw)
        # Remove semicolon comments: ;comment
        if ";" in clean:
            clean = clean.split(";", 1)[0]
        clean = clean.strip()
        if not clean:
            return ""

        # FluidNC compatibility: replace M400 with G4 P0.05
        if re.match(r"^M400\b", clean, re.IGNORECASE):
            return "G4 P0.05"

        # Check for Z movement
        z_match = re.search(r"\bZ([-\d\.]+)", clean, re.IGNORECASE)
        if z_match:
            # If Z moves, check feedrate
            f_match = re.search(r"\bF([-\d\.]+)", clean, re.IGNORECASE)
            if f_match:
                feed_val = float(f_match.group(1))
                if feed_val > cls.MAX_Z_FEED:
                    # Clamp feedrate to 1000
                    clean = re.sub(r"\bF[-\d\.]+", f"F{cls.MAX_Z_FEED}", clean, flags=re.IGNORECASE)
                    logger.debug(f"Clamped Z feedrate {feed_val} -> {cls.MAX_Z_FEED} in: {raw}")
            else:
                # If moving Z without explicit F on a G1/G2/G3 line, force safe F1000
                if re.match(r"^G[123]\b", clean, re.IGNORECASE):
                    clean = f"{clean} F{cls.MAX_Z_FEED}"

        return clean

    @classmethod
    def prepare_job(cls, gcode_content, job_name="Job"):
        """
        Parses multi-line G-code string or list, sanitizes every line,
        and returns a validated job dictionary.
        """
        if isinstance(gcode_content, str):
            raw_lines = gcode_content.splitlines()
        else:
            raw_lines = list(gcode_content)

        clean_lines = []
        for line in raw_lines:
            sanitized = cls.sanitize_line(line)
            if sanitized:
                clean_lines.append(sanitized)

        job_id = f"job_{int(time.time())}_{uuid.uuid4().hex[:6]}"
        return {
            "job_id": job_id,
            "name": job_name,
            "lines": clean_lines,
            "total_lines": len(clean_lines),
            "status": "ready"
        }

    # -----------------------------------------------------------------------
    # Stream Execution
    # -----------------------------------------------------------------------
    def start_job(self, job_dict):
        """Starts streaming the provided job in a background thread."""
        with self.lock:
            if not self.driver.connected:
                return False, "Machine not connected"
            if self.is_streaming:
                return False, "Another job is currently streaming"

            self.current_job = job_dict
            self.is_streaming = True
            self.is_paused = False
            self._abort_requested = False
            self._pause_event.set()

            self.current_job["status"] = "streaming"
            self.current_job["start_time"] = time.time()
            self.current_job["current_line"] = 0
            self.current_job["percent"] = 0.0

            self._worker_thread = threading.Thread(target=self._stream_worker, daemon=True)
            self._worker_thread.start()
            return True, f"Started job {job_dict['job_id']}"

    def pause(self):
        """Pauses stream and sends realtime feed hold '!' to FluidNC."""
        with self.lock:
            if not self.is_streaming or self.is_paused:
                return False
            self.is_paused = True
            self._pause_event.clear()
            self.driver.send_realtime("!")
            if self.current_job:
                self.current_job["status"] = "paused"
            logger.info("Stream paused (!)")
            return True

    def resume(self):
        """Resumes stream and sends realtime cycle start '~' to FluidNC."""
        with self.lock:
            if not self.is_streaming or not self.is_paused:
                return False
            self.is_paused = False
            self._pause_event.set()
            self.driver.send_realtime("~")
            if self.current_job:
                self.current_job["status"] = "streaming"
            logger.info("Stream resumed (~)")
            return True

    def abort(self):
        """Aborts stream, sends realtime reset (0x18), and lifts pen."""
        with self.lock:
            if not self.is_streaming:
                return False
            self._abort_requested = True
            self.is_paused = False
            self._pause_event.set()
            self.driver.send_realtime(0x18)
            if self.current_job:
                self.current_job["status"] = "aborted"
            logger.warning("Stream aborted (Ctrl-X)")
            return True

    def _stream_worker(self):
        """Worker thread executing Ping-Pong stream loop."""
        job = self.current_job
        lines = job["lines"]
        total = len(lines)
        t_start = job["start_time"]
        success = True

        logger.info(f"Stream worker started for {job['job_id']} ({total} lines)")

        for idx, line in enumerate(lines):
            # 1. Check for Abort
            if self._abort_requested or not self.driver.connected:
                success = False
                break

            # 2. Check for Pause
            self._pause_event.wait()
            if self._abort_requested:
                success = False
                break

            # 3. Send line to machine
            line_idx = idx + 1
            ok, resp = self.driver.send_line(line)

            if not ok:
                logger.error(f"Error on line {line_idx}: {line} -> {resp}")
                for cb in self._error_callbacks:
                    try:
                        cb(job["job_id"], line_idx, line, resp)
                    except Exception:
                        pass

            # 4. Calculate progress
            now = time.time()
            elapsed = now - t_start
            pct = round((line_idx / total) * 100.0, 2)
            lines_per_sec = (line_idx / elapsed) if elapsed > 0.5 else 1.0
            rem_lines = total - line_idx
            est_rem = round(rem_lines / lines_per_sec, 1)

            progress_payload = {
                "job_id": job["job_id"],
                "current_line": line_idx,
                "total_lines": total,
                "percent": pct,
                "active_command": line,
                "elapsed_seconds": round(elapsed, 1),
                "estimated_remaining_seconds": est_rem
            }

            with self.lock:
                job["current_line"] = line_idx
                job["percent"] = pct

            for cb in self._progress_callbacks:
                try:
                    cb(progress_payload)
                except Exception as e:
                    logger.error(f"Progress callback error: {e}")

        # Post-job cleanup
        elapsed_total = round(time.time() - t_start, 2)
        if success and not self._abort_requested:
            # Send final safe pen up and buffer drain
            self.driver.send_line(f"G0 Z{self.SAFE_PEN_UP_Z:.2f} F{self.MAX_Z_FEED}")
            self.driver.send_line("G4 P0.05")
            job["status"] = "completed"
            logger.info(f"Job {job['job_id']} completed successfully in {elapsed_total}s")
        else:
            job["status"] = "aborted"
            logger.warning(f"Job {job['job_id']} ended early with status: {job['status']}")

        with self.lock:
            self.is_streaming = False
            self.is_paused = False

        complete_payload = {
            "job_id": job["job_id"],
            "status": job["status"],
            "duration_seconds": elapsed_total
        }
        for cb in self._complete_callbacks:
            try:
                cb(complete_payload)
            except Exception as e:
                logger.error(f"Complete callback error: {e}")

    def get_status(self):
        with self.lock:
            if not self.current_job:
                return {
                    "is_streaming": False,
                    "is_paused": False,
                    "job": None
                }
            return {
                "is_streaming": self.is_streaming,
                "is_paused": self.is_paused,
                "job": {
                    "job_id": self.current_job.get("job_id"),
                    "name": self.current_job.get("name"),
                    "status": self.current_job.get("status"),
                    "total_lines": self.current_job.get("total_lines", 0),
                    "current_line": self.current_job.get("current_line", 0),
                    "percent": self.current_job.get("percent", 0.0)
                }
            }

