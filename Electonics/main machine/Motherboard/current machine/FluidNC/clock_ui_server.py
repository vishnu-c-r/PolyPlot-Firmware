#!/usr/bin/env python3
"""
Spirit Live Water Clock & Machine Control Dashboard
=====================================================
A high-performance web dashboard running locally on the Raspberry Pi / PC
for live G-code generation, machine monitoring, and continuous water clock operation.

Features:
- Single-file zero-dependency web server (using Bottle + Vanilla JS/HTML5 Canvas)
- Live G-code generator for Circle, Square, Minimal, and Digits-only clock styles
- Real-time 2D Spirit bed visualizer with water evaporation animation & live toolhead
- Background scheduler syncing draws precisely to the :00 second mark of each minute
- Machine monitoring with status updates (? query, MPos coordinates, Idle/Run state)
- Full hardware controls: Homing ($H), Feed Hold (!), Resume (~), Pen Up/Down, Park, Manual G-code

Usage:
  python clock_ui_server.py [--port 8080] [--serial COM14] [--baud 115200]
"""

import argparse
import datetime
import json
import math
import os
import re
import socket
import sys
import threading
import time

try:
    import serial
    import serial.tools.list_ports
    HAVE_SERIAL = True
except ImportError:
    HAVE_SERIAL = False

import bottle

# ---------------------------------------------------------------------------
# Safe Machine Corners for Parking & Boundaries
# ---------------------------------------------------------------------------
SAFE_CORNERS = [
    (-680.0, 280.0),  # Top-Left
    (-20.0, 280.0),   # Top-Right
    (-680.0, 20.0),   # Bottom-Left
    (-20.0, 20.0),    # Bottom-Right
]

def get_closest_corner(x, y):
    """Finds the closest safe hardware corner to coordinate (x, y)."""
    return min(SAFE_CORNERS, key=lambda c: (c[0] - x) ** 2 + (c[1] - y) ** 2)

# Curated Mindful / Inspirational Short Quotes (Bed scaled)
DEFAULT_QUOTES = [
    "TIME FLOWS LIKE WATER",
    "EVERY SECOND COUNTS",
    "BREATHE AND BEGIN",
    "MAKE TODAY COUNT",
    "BE HERE NOW",
    "STILL WATERS RUN DEEP",
    "NOW IS THE TIME",
    "CREATE YOUR TOMORROW",
    "FLOW WITH TIME",
    "MOMENTS BECOME MEMORIES",
    "ENERGY FLOWS WITH FOCUS",
    "A FRESH START EVERY MINUTE",
]

# ---------------------------------------------------------------------------
# Hershey Single-Stroke Font (Full Vector A-Z, 0-9, Punctuation)
# ---------------------------------------------------------------------------
HERSHEY_GLYPHS = {
    'A': [[(0, 0), (6, 20), (12, 0)], [(2, 7), (10, 7)]],
    'B': [[(0, 0), (0, 20), (8, 20), (12, 16), (8, 11), (0, 11)], [(8, 11), (12, 6), (8, 0), (0, 0)]],
    'C': [[(12, 16), (8, 20), (4, 20), (0, 14), (0, 6), (4, 0), (8, 0), (12, 4)]],
    'D': [[(0, 0), (0, 20), (6, 20), (12, 14), (12, 6), (6, 0), (0, 0)]],
    'E': [[(11, 0), (0, 0), (0, 20), (11, 20)], [(0, 10), (8, 10)]],
    'F': [[(0, 0), (0, 20), (11, 20)], [(0, 10), (8, 10)]],
    'G': [[(12, 16), (8, 20), (4, 20), (0, 14), (0, 6), (4, 0), (8, 0), (12, 4), (12, 10), (6, 10)]],
    'H': [[(0, 0), (0, 20)], [(12, 0), (12, 20)], [(0, 10), (12, 10)]],
    'I': [[(3, 0), (9, 0)], [(6, 0), (6, 20)], [(3, 20), (9, 20)]],
    'J': [[(0, 6), (4, 0), (8, 0), (10, 4), (10, 20), (6, 20)]],
    'K': [[(0, 0), (0, 20)], [(12, 20), (0, 9)], [(4, 12), (12, 0)]],
    'L': [[(0, 20), (0, 0), (11, 0)]],
    'M': [[(0, 0), (0, 20), (6, 8), (12, 20), (12, 0)]],
    'N': [[(0, 0), (0, 20), (12, 0), (12, 20)]],
    'O': [[(4, 0), (0, 6), (0, 14), (4, 20), (8, 20), (12, 14), (12, 6), (8, 0), (4, 0)]],
    'P': [[(0, 0), (0, 20), (8, 20), (12, 16), (8, 11), (0, 11)]],
    'Q': [[(4, 0), (0, 6), (0, 14), (4, 20), (8, 20), (12, 14), (12, 6), (8, 0), (4, 0)], [(6, 5), (13, -2)]],
    'R': [[(0, 0), (0, 20), (8, 20), (12, 16), (8, 11), (0, 11)], [(6, 11), (12, 0)]],
    'S': [[(0, 4), (4, 0), (8, 0), (12, 4), (12, 8), (8, 11), (4, 11), (0, 14), (0, 18), (4, 20), (8, 20), (12, 17)]],
    'T': [[(6, 0), (6, 20)], [(0, 20), (12, 20)]],
    'U': [[(0, 20), (0, 5), (4, 0), (8, 0), (12, 5), (12, 20)]],
    'V': [[(0, 20), (6, 0), (12, 20)]],
    'W': [[(0, 20), (3, 0), (6, 12), (9, 0), (12, 20)]],
    'X': [[(0, 0), (12, 20)], [(0, 20), (12, 0)]],
    'Y': [[(6, 0), (6, 10), (0, 20)], [(6, 10), (12, 20)]],
    'Z': [[(0, 20), (12, 20), (0, 0), (12, 0)]],
    '0': [[(4, 0), (0, 6), (0, 14), (4, 20), (8, 20), (12, 14), (12, 6), (8, 0), (4, 0)], [(2, 3), (10, 17)]],
    '1': [[(2, 14), (6, 20), (6, 0), (2, 0), (10, 0)]],
    '2': [[(0, 15), (3, 19), (8, 20), (12, 16), (12, 12), (0, 0), (12, 0)]],
    '3': [[(2, 17), (7, 20), (11, 17), (9, 11), (5, 11), (9, 11), (12, 7), (10, 2), (5, 0), (0, 2)]],
    '4': [[(9, 0), (9, 20), (0, 6), (13, 6)]],
    '5': [[(11, 20), (2, 20), (2, 11), (6, 13), (11, 10), (11, 4), (7, 0), (1, 1)]],
    '6': [[(10, 17), (5, 12), (1, 6), (3, 0), (8, 0), (12, 4), (11, 10), (6, 10), (2, 7)]],
    '7': [[(0, 20), (12, 20), (5, 0)]],
    '8': [[(6, 10), (2, 13), (2, 17), (6, 20), (10, 17), (10, 13), (6, 10), (2, 6), (2, 2), (6, 0), (10, 2), (10, 6), (6, 10)]],
    '9': [[(2, 3), (7, 8), (11, 14), (9, 20), (4, 20), (0, 16), (1, 10), (6, 10), (10, 13)]],
    ':': [[(6, 14), (6, 12)], [(6, 6), (6, 4)]],
    '-': [[(2, 10), (10, 10)]],
    '.': [[(5, 0), (6, 2), (7, 0), (5, 0)]],
    ',': [[(6, 2), (7, 0), (5, -3)]],
    '!': [[(6, 20), (6, 6)], [(6, 2), (6, 0)]],
    '?': [[(1, 15), (4, 20), (9, 19), (11, 15), (10, 11), (6, 8), (6, 5)], [(6, 2), (6, 0)]],
    "'": [[(6, 20), (6, 14)]]
}
for c in range(65, 91):
    upper = chr(c)
    lower = chr(c + 32)
    if lower not in HERSHEY_GLYPHS and upper in HERSHEY_GLYPHS:
        HERSHEY_GLYPHS[lower] = HERSHEY_GLYPHS[upper]

HERSHEY_DIGITS = HERSHEY_GLYPHS

# ---------------------------------------------------------------------------
# G-Code & Path Generation
# ---------------------------------------------------------------------------
def generate_clock_gcode_and_paths(dt, config):
    """
    Generates both G-code string list and 2D vector path segments for the UI canvas.
    """
    style = config.get("style", "circle")
    cx = float(config.get("center_x", -350.0))
    cy = float(config.get("center_y", 150.0))
    radius = float(config.get("radius", 110.0))
    z_up = float(config.get("z_up", -0.5))
    z_down = float(config.get("z_down", -4.5))
    feed_xy = int(config.get("feed_xy", 12000))
    feed_z = int(config.get("feed_z", 1000))
    rapid_xy = int(config.get("rapid_xy", 12000))
    reset_wco = bool(config.get("reset_wco", True))
    mirror_x = bool(config.get("mirror_x", True))

    def mx(x):
        return (2.0 * cx - x) if mirror_x else x

    time_str = dt.strftime("%H:%M")
    lines = []
    vector_strokes = []  # list of polylines: [[(x,y), (x,y), ...], ...]

    lines.append(f"; Spirit Clock - {time_str} ({style.upper()}) [MIRRORED: {mirror_x}]")
    lines.append(f"; Center: X={cx:.1f}, Y={cy:.1f}, Radius={radius:.1f}mm")
    lines.append(f"; Z_UP={z_up:.2f}, Z_DOWN={z_down:.2f}")
    lines.append("G21 ; Millimeters")
    lines.append("G90 ; Absolute Coordinates")
    if reset_wco:
        lines.append("G92.1 ; Reset Temporary Offsets")
    lines.append(f"G0 Z{z_up:.2f} F{feed_z} ; Safe Pen Height")
    lines.append("")

    total_dist = 0.0
    pen_lifts = 0

    # 1. Outer Frame
    if style == "circle":
        start_x = cx
        start_y = cy + radius
        arc_cmd = "G3" if mirror_x else "G2"
        lines.append("; --- Outer Ring ---")
        lines.append(f"G0 X{mx(start_x):.3f} Y{start_y:.3f} F{rapid_xy}")
        lines.append(f"G1 Z{z_down:.2f} F{feed_z}")
        lines.append(f"{arc_cmd} X{mx(start_x):.3f} Y{start_y:.3f} I0 J{-radius:.3f} F{feed_xy}")
        lines.append(f"G0 Z{z_up:.2f} F{feed_z}")
        total_dist += 2 * math.pi * radius
        pen_lifts += 1

        # Circle polyline for UI
        ring_pts = []
        for i in range(65):
            th = math.radians(i * 360.0 / 64)
            ring_pts.append((mx(cx + radius * math.cos(th)), cy + radius * math.sin(th)))
        vector_strokes.append({"type": "frame", "pts": ring_pts})

        # 12 Ticks
        major_tick = radius * 0.13
        minor_tick = radius * 0.07
        lines.append("; --- 12 Hour Ticks ---")
        for hour in range(12):
            ang = math.radians(90 - (hour * 30))
            tick_len = major_tick if (hour % 3 == 0) else minor_tick
            r_out = radius - 3.0
            r_in = r_out - tick_len

            x_out = cx + r_out * math.cos(ang)
            y_out = cy + r_out * math.sin(ang)
            x_in = cx + r_in * math.cos(ang)
            y_in = cy + r_in * math.sin(ang)

            lines.append(f"G0 X{mx(x_out):.3f} Y{y_out:.3f} F{rapid_xy}")
            lines.append(f"G1 Z{z_down:.2f} F{feed_z}")
            lines.append(f"G1 X{mx(x_in):.3f} Y{y_in:.3f} F{feed_xy}")
            lines.append(f"G0 Z{z_up:.2f} F{feed_z}")
            total_dist += tick_len
            pen_lifts += 1
            vector_strokes.append({"type": "tick", "pts": [(mx(x_out), y_out), (mx(x_in), y_in)]})
        lines.append("")

    elif style == "square":
        half = radius
        x_min, x_max = cx - half, cx + half
        y_min, y_max = cy - half, cy + half
        lines.append("; --- Clean Square Outer Frame (Single Continuous Loop) ---")
        lines.append(f"G0 X{mx(x_min):.3f} Y{y_min:.3f} F{rapid_xy}")
        lines.append(f"G1 Z{z_down:.2f} F{feed_z}")
        lines.append(f"G1 X{mx(x_max):.3f} Y{y_min:.3f} F{feed_xy}")
        lines.append(f"G1 X{mx(x_max):.3f} Y{y_max:.3f} F{feed_xy}")
        lines.append(f"G1 X{mx(x_min):.3f} Y{y_max:.3f} F{feed_xy}")
        lines.append(f"G1 X{mx(x_min):.3f} Y{y_min:.3f} F{feed_xy}")
        lines.append(f"G0 Z{z_up:.2f} F{feed_z}")
        total_dist += 8 * half
        pen_lifts += 1
        vector_strokes.append({
            "type": "frame",
            "pts": [(mx(x_min), y_min), (mx(x_max), y_min), (mx(x_max), y_max), (mx(x_min), y_max), (mx(x_min), y_min)]
        })
        lines.append("")

    # 2. Clock Hands (Circle and Minimal only; Square is clean & minimal with digital digits!)
    if style in ("circle", "minimal"):
        hour_val = dt.hour % 12 + dt.minute / 60.0
        min_val = dt.minute

        hour_ang = math.radians(90.0 - (hour_val * 30.0))
        min_ang = math.radians(90.0 - (min_val * 6.0))

        r_hour = radius * 0.48
        r_min = radius * 0.75

        hx = cx + r_hour * math.cos(hour_ang)
        hy = cy + r_hour * math.sin(hour_ang)
        mx_pt = cx + r_min * math.cos(min_ang)
        my = cy + r_min * math.sin(min_ang)

        lines.append("; --- Continuous Hour & Minute Hands ---")
        lines.append(f"G0 X{mx(cx):.3f} Y{cy:.3f} F{rapid_xy}")
        lines.append(f"G1 Z{z_down:.2f} F{feed_z}")
        lines.append(f"G1 X{mx(hx):.3f} Y{hy:.3f} F{feed_xy} ; Hour Hand")
        lines.append(f"G1 X{mx(cx):.3f} Y{cy:.3f} F{feed_xy} ; Center")
        lines.append(f"G1 X{mx(mx_pt):.3f} Y{my:.3f} F{feed_xy} ; Minute Hand")
        lines.append(f"G0 Z{z_up:.2f} F{feed_z}")
        total_dist += 2 * r_hour + r_min
        pen_lifts += 1
        vector_strokes.append({
            "type": "hand",
            "pts": [(mx(cx), cy), (mx(hx), hy), (mx(cx), cy), (mx(mx_pt), my)]
        })
        lines.append("")

    # 3. Hershey Digital Text & Quote
    if style == "quote":
        # 1. Digital Time in Upper Half
        digit_h = 44.0
        digit_w = digit_h * 0.58
        digit_sp = digit_w * 0.28
        total_time_w = len(time_str) * digit_w + (len(time_str) - 1) * digit_sp
        time_x = cx - (total_time_w / 2.0)
        time_y = cy + 25.0

        scale_dx = digit_w / 12.0
        scale_dy = digit_h / 20.0
        cur_x = time_x

        lines.append(f"; --- Digits '{time_str}' ---")
        for char in time_str:
            if char in HERSHEY_GLYPHS:
                for stroke in HERSHEY_GLYPHS[char]:
                    stroke_pts = []
                    p0 = stroke[0]
                    gx0 = mx(cur_x + p0[0] * scale_dx)
                    gy0 = time_y + p0[1] * scale_dy
                    stroke_pts.append((gx0, gy0))

                    lines.append(f"G0 X{gx0:.3f} Y{gy0:.3f} F{rapid_xy}")
                    lines.append(f"G1 Z{z_down:.2f} F{feed_z}")
                    pen_lifts += 1

                    prev_x, prev_y = gx0, gy0
                    for pt in stroke[1:]:
                        gx = mx(cur_x + pt[0] * scale_dx)
                        gy = time_y + pt[1] * scale_dy
                        lines.append(f"G1 X{gx:.3f} Y{gy:.3f} F{feed_xy}")
                        total_dist += math.hypot(gx - prev_x, gy - prev_y)
                        prev_x, prev_y = gx, gy
                        stroke_pts.append((gx, gy))

                    lines.append(f"G0 Z{z_up:.2f} F{feed_z}")
                    vector_strokes.append({"type": "digit", "pts": stroke_pts})
            cur_x += digit_w + digit_sp

        # 2. Quote Text in Lower Half
        raw_quote = (config.get("quote_text") or "").strip().upper()
        if not raw_quote or config.get("quote_rotate", True):
            quote_text = DEFAULT_QUOTES[dt.minute % len(DEFAULT_QUOTES)]
        else:
            quote_text = raw_quote

        lines.append(f"; --- Quote '{quote_text}' ---")
        q_h = 22.0
        q_w = q_h * 0.58
        q_sp = q_w * 0.28
        space_w = q_w * 0.70

        total_q_w = 0.0
        for i, ch in enumerate(quote_text):
            if ch == ' ':
                total_q_w += space_w
            else:
                total_q_w += q_w
            if i < len(quote_text) - 1 and ch != ' ':
                total_q_w += q_sp

        max_allowed_w = 620.0
        if total_q_w > max_allowed_w:
            ratio = max_allowed_w / total_q_w
            q_h *= ratio
            q_w *= ratio
            q_sp *= ratio
            space_w *= ratio
            total_q_w = max_allowed_w

        quote_x = cx - (total_q_w / 2.0)
        quote_y = cy - 65.0
        scale_qx = q_w / 12.0
        scale_qy = q_h / 20.0

        cur_qx = quote_x
        for i, ch in enumerate(quote_text):
            if ch == ' ':
                cur_qx += space_w
                continue
            if ch in HERSHEY_GLYPHS:
                for stroke in HERSHEY_GLYPHS[ch]:
                    stroke_pts = []
                    p0 = stroke[0]
                    gx0 = mx(cur_qx + p0[0] * scale_qx)
                    gy0 = quote_y + p0[1] * scale_qy
                    stroke_pts.append((gx0, gy0))

                    lines.append(f"G0 X{gx0:.3f} Y{gy0:.3f} F{rapid_xy}")
                    lines.append(f"G1 Z{z_down:.2f} F{feed_z}")
                    pen_lifts += 1

                    prev_x, prev_y = gx0, gy0
                    for pt in stroke[1:]:
                        gx = mx(cur_qx + pt[0] * scale_qx)
                        gy = quote_y + pt[1] * scale_qy
                        lines.append(f"G1 X{gx:.3f} Y{gy:.3f} F{feed_xy}")
                        total_dist += math.hypot(gx - prev_x, gy - prev_y)
                        prev_x, prev_y = gx, gy
                        stroke_pts.append((gx, gy))

                    lines.append(f"G0 Z{z_up:.2f} F{feed_z}")
                    vector_strokes.append({"type": "quote", "pts": stroke_pts})
            cur_qx += q_w + q_sp

        lines.append("")

    elif style != "circle":
        lines.append(f"; --- Digital Text '{time_str}' ---")
        if style in ("digits", "square"):
            char_h = radius * 0.52
            char_w = char_h * 0.58
            char_spacing = char_w * 0.28
            total_text_w = len(time_str) * char_w + (len(time_str) - 1) * char_spacing
            text_x = cx - (total_text_w / 2.0)
            text_y = cy - (char_h / 2.0)
        else:
            char_h = radius * 0.22
            char_w = char_h * 0.60
            char_spacing = char_w * 0.32
            total_text_w = len(time_str) * char_w + (len(time_str) - 1) * char_spacing
            text_x = cx - (total_text_w / 2.0)
            text_y = cy - (radius * 0.52)

        cur_x = text_x
        scale_x = char_w / 12.0
        scale_y = char_h / 20.0

        for char in time_str:
            if char in HERSHEY_GLYPHS:
                for stroke in HERSHEY_GLYPHS[char]:
                    stroke_pts = []
                    p0 = stroke[0]
                    gx0 = mx(cur_x + p0[0] * scale_x)
                    gy0 = text_y + p0[1] * scale_y
                    stroke_pts.append((gx0, gy0))

                    lines.append(f"G0 X{gx0:.3f} Y{gy0:.3f} F{rapid_xy}")
                    lines.append(f"G1 Z{z_down:.2f} F{feed_z}")
                    pen_lifts += 1

                    prev_x, prev_y = gx0, gy0
                    for pt in stroke[1:]:
                        gx = mx(cur_x + pt[0] * scale_x)
                        gy = text_y + pt[1] * scale_y
                        lines.append(f"G1 X{gx:.3f} Y{gy:.3f} F{feed_xy}")
                        total_dist += math.hypot(gx - prev_x, gy - prev_y)
                        prev_x, prev_y = gx, gy
                        stroke_pts.append((gx, gy))

                    lines.append(f"G0 Z{z_up:.2f} F{feed_z}")
                    vector_strokes.append({"type": "digit", "pts": stroke_pts})

            cur_x += char_w + char_spacing

        lines.append("")

    lines.append("; --- End Cycle ---")
    lines.append(f"G0 Z{z_up:.2f} F{feed_z}")
    if vector_strokes and vector_strokes[-1].get("pts"):
        last_pt = vector_strokes[-1]["pts"][-1]
        park_x, park_y = get_closest_corner(last_pt[0], last_pt[1])
    else:
        park_x, park_y = get_closest_corner(cx, cy)
    lines.append(f"G0 X{park_x:.3f} Y{park_y:.3f} F{rapid_xy} ; Park Closest Corner")
    lines.append("G4 P0.05 ; Buffer drain")

    est_sec = (total_dist / feed_xy) * 60 + pen_lifts * 2 * (abs(z_up - z_down) / feed_z) * 60 + pen_lifts * 0.10

    return lines, vector_strokes, total_dist, est_sec


# ---------------------------------------------------------------------------
# Machine Manager (Serial / Telnet Communication + Background Thread)
# ---------------------------------------------------------------------------
class MachineManager:
    def __init__(self):
        self.lock = threading.RLock()
        self.serial_conn = None
        self.socket_conn = None
        self.connected = False
        self.conn_type = "none"
        self.conn_address = ""

        # Machine State
        self.state = "Disconnected"  # Idle, Run, Hold, Alarm, Disconnected
        self.mpos = [-350.0, 150.0, -0.5]
        self.last_query_time = 0
        self.last_response_time = 0

        # Clock Loop State
        self.clock_active = False
        self.current_job_active = False
        self.current_job_progress = 0.0
        self.current_job_line = ""
        self.current_job_total_lines = 0
        self.current_job_current_line_idx = 0
        self.last_job_duration = 0.0
        self.last_drawn_time = ""
        self.draw_start_time = 0.0

        # Configuration (Speeds set to hardware maximums by default)
        self.config = {
            "style": "circle",
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

        # Terminal Log
        self.logs = []
        self._log("System initialized. Select a port or enter IP to connect.")

        # Auto-reconnect configuration
        self.auto_reconnect = True
        self.preferred_target = "auto"
        self.preferred_baud = 115200
        self._last_reconnect_log = 0.0

        # Start background threads (only if running as main server)
        if __name__ == '__main__':
            self.scheduler_thread = threading.Thread(target=self._scheduler_loop, daemon=True)
            self.scheduler_thread.start()

            self.poller_thread = threading.Thread(target=self._status_poller_loop, daemon=True)
            self.poller_thread.start()

            self.watchdog_thread = threading.Thread(target=self._watchdog_loop, daemon=True)
            self.watchdog_thread.start()

    def _log(self, msg):
        ts = datetime.datetime.now().strftime("%H:%M:%S")
        entry = f"[{ts}] {msg}"
        with self.lock:
            self.logs.append(entry)
            if len(self.logs) > 100:
                self.logs.pop(0)
        print(entry)

    def get_ports(self):
        if not HAVE_SERIAL:
            return []
        ports = []
        for p in serial.tools.list_ports.comports():
            ports.append({"device": p.device, "description": p.description or p.device})
        return ports

    def _watchdog_loop(self):
        """Continuously probes and restores connection if machine is rebooted or unplugged."""
        while True:
            time.sleep(1.5)
            if self.connected or not self.auto_reconnect:
                continue

            ports = self.get_ports()
            if not ports:
                continue

            candidate = None
            for p in ports:
                desc = (p.get("description") or "").lower()
                dev = (p.get("device") or "").lower()
                if "ch340" in desc or "cp210" in desc or "usb-serial" in desc or "esp32" in desc or "ttyusb" in dev or "ttyacm" in dev:
                    candidate = p["device"]
                    break

            if not candidate and ports:
                candidate = ports[0]["device"]

            if candidate:
                now = time.time()
                if now - self._last_reconnect_log > 8.0:
                    self._log(f"Watchdog: Detected machine on {candidate}. Auto-reconnecting...")
                    self._last_reconnect_log = now
                ok, msg = self.connect(candidate, self.preferred_baud)
                if ok:
                    self._log(f"Watchdog: Successfully reconnected to {candidate}!")

    def connect(self, target, baud=115200):
        with self.lock:
            self.preferred_target = target
            self.preferred_baud = baud
            self.auto_reconnect = True
            if self.connected:
                self._disconnect_unlocked()

            target = target.strip()
            # Check if IP address
            is_ip = re.match(r"^\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}$", target)

            if is_ip:
                self._log(f"Connecting to Telnet at {target}:23...")
                try:
                    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                    s.settimeout(4.0)
                    s.connect((target, 23))
                    self.socket_conn = s
                    self.connected = True
                    self.conn_type = "telnet"
                    self.conn_address = target
                    self.state = "Idle"
                    self._log(f"Connected to {target} via Telnet!")
                    return True, "Connected successfully"
                except Exception as e:
                    self._log(f"Telnet connection error: {e}")
                    return False, str(e)
            else:
                # Serial
                if not HAVE_SERIAL:
                    return False, "pyserial not installed"
                available_devices = [p["device"] for p in self.get_ports()]
                if target.lower() == "auto" or target not in available_devices:
                    ports = self.get_ports()
                    for p in ports:
                        desc = (p.get("description") or "").lower()
                        if "ch340" in desc or "cp210" in desc or "usb-serial" in desc or "esp32" in desc:
                            target = p["device"]
                            break
                    if (target.lower() == "auto" or target not in available_devices) and ports:
                        target = ports[0]["device"]
                    if target.lower() == "auto" or target not in available_devices:
                        return False, "No active serial ports found"

                self._log(f"Opening Serial port {target} at {baud} baud...")
                try:
                    ser = serial.Serial(target, baud, timeout=0.5)
                    time.sleep(1.0)
                    ser.reset_input_buffer()
                    self.serial_conn = ser
                    self.connected = True
                    self.conn_type = "serial"
                    self.conn_address = target
                    self.state = "Idle"
                    self._log(f"Connected to {target} via Serial!")
                    return True, "Connected successfully"
                except Exception as e:
                    err_str = str(e)
                    if "PermissionError" in err_str or "Access is denied" in err_str:
                        msg = f"Port '{target}' is busy (Access denied). Please close VS Code Serial Monitor or any other terminal and retry."
                    else:
                        msg = f"Serial error on {target}: {e}"
                    self._log(msg)
                    return False, msg

    def disconnect(self):
        with self.lock:
            self.auto_reconnect = False
            self._disconnect_unlocked()

    def _disconnect_unlocked(self):
        self.clock_active = False
        if self.serial_conn:
            try:
                self.serial_conn.close()
            except Exception:
                pass
            self.serial_conn = None
        if self.socket_conn:
            try:
                self.socket_conn.close()
            except Exception:
                pass
            self.socket_conn = None
        self.connected = False
        self.state = "Disconnected"
        self._log("Disconnected from machine.")

    def send_command(self, cmd):
        """Sends a single G-code line and returns the response."""
        with self.lock:
            return self._send_command_unlocked(cmd)

    def _send_command_unlocked(self, cmd):
        if not self.connected:
            return "Error: Not connected"

        cmd_clean = cmd.strip()

        # Real-time character commands (?, !, ~, \x18) do not expect \n or trailing "ok"
        if cmd_clean in ("?", "!", "~") or len(cmd_clean) == 1 and ord(cmd_clean) < 32:
            try:
                if self.conn_type == "serial" and self.serial_conn:
                    self.serial_conn.write(cmd_clean.encode("utf-8"))
                    time.sleep(0.04)
                    return self.serial_conn.read_all().decode("utf-8", errors="ignore").strip()
                elif self.conn_type == "telnet" and self.socket_conn:
                    self.socket_conn.sendall(cmd_clean.encode("utf-8"))
                    time.sleep(0.04)
                    return self.socket_conn.recv(512).decode("utf-8", errors="ignore").strip()
            except Exception as e:
                self._log(f"Comm error: {e}")
                self._disconnect_unlocked()
                return f"Comm Error: {e}"
            return "ok"

        data = (cmd_clean + "\n").encode("utf-8")

        try:
            if self.conn_type == "serial" and self.serial_conn:
                self.serial_conn.write(data)
                resp_lines = []
                t0 = time.time()
                while time.time() - t0 < 3.0:
                    raw = self.serial_conn.readline().decode("utf-8", errors="ignore").strip()
                    if raw:
                        resp_lines.append(raw)
                        if raw.startswith("ok") or raw.startswith("error:"):
                            break
                return "\n".join(resp_lines)
            elif self.conn_type == "telnet" and self.socket_conn:
                self.socket_conn.sendall(data)
                resp = ""
                t0 = time.time()
                while time.time() - t0 < 3.0:
                    chunk = self.socket_conn.recv(1024).decode("utf-8", errors="ignore")
                    if chunk:
                        resp += chunk
                        if "ok\n" in resp or resp.endswith("ok") or "error:" in resp:
                            break
                return resp.strip()
        except Exception as e:
            self._log(f"Comm error: {e}")
            self._disconnect_unlocked()
            return f"Comm Error: {e}"
        return "ok"

    def _status_poller_loop(self):
        """Polls FluidNC status '?' every 300ms when connected and not actively streaming."""
        while True:
            time.sleep(0.3)
            if not self.connected or self.current_job_active:
                continue

            with self.lock:
                if not self.connected or self.current_job_active:
                    continue
                try:
                    if self.conn_type == "serial" and self.serial_conn:
                        self.serial_conn.write(b"?")
                        time.sleep(0.04)
                        resp = self.serial_conn.read_all().decode("utf-8", errors="ignore")
                    elif self.conn_type == "telnet" and self.socket_conn:
                        self.socket_conn.sendall(b"?")
                        time.sleep(0.04)
                        resp = self.socket_conn.recv(1024).decode("utf-8", errors="ignore")
                    else:
                        resp = ""

                    # Parse Grbl status: <Idle|MPos:-350.000,150.000,-0.500|...>
                    match = re.search(r"<([A-Za-z]+)\|MPos:([-\d\.]+),([-\d\.]+),([-\d\.]+)", resp)
                    if match:
                        self.state = match.group(1)
                        self.mpos = [float(match.group(2)), float(match.group(3)), float(match.group(4))]
                        self.last_response_time = time.time()
                except Exception as e:
                    self._log(f"Connection lost in poller: {e}")
                    self._disconnect_unlocked()

    def _scheduler_loop(self):
        """Handles the 1-minute live clock execution."""
        while True:
            time.sleep(0.1)
            if not self.clock_active or not self.connected or self.current_job_active:
                continue

            now = datetime.datetime.now()
            # Trigger exactly on second 0 of each minute
            if now.second == 0 and now.microsecond < 300_000:
                time_str = now.strftime("%H:%M")
                if time_str != self.last_drawn_time:
                    self.execute_clock_cycle(now)

    def stream_gcode_lines(self, lines, job_title="Custom Art Plot"):
        """Streams arbitrary G-code lines in a background thread."""
        if not self.connected:
            return False, "Machine not connected"
        if self.current_job_active:
            return False, "Another drawing job is currently active"
        t = threading.Thread(target=self._stream_lines_worker, args=(lines, job_title), daemon=True)
        t.start()
        return True, "Streaming started"

    def _stream_lines_worker(self, lines, job_title):
        self._log(f"Starting {job_title} ({len(lines)} lines)...")
        self.current_job_active = True
        self.current_job_total_lines = len(lines)
        self.current_job_current_line_idx = 0
        self.draw_start_time = time.time()
        self.last_drawn_time = job_title

        for idx, line in enumerate(lines):
            if not self.connected or not self.current_job_active:
                break

            line_clean = line.strip()
            self.current_job_current_line_idx = idx + 1
            self.current_job_progress = (idx + 1) / float(len(lines)) * 100.0
            self.current_job_line = line_clean

            if line_clean and not line_clean.startswith(";"):
                with self.lock:
                    if not self.connected:
                        break
                    try:
                        if self.conn_type == "serial" and self.serial_conn:
                            self.serial_conn.write((line_clean + "\n").encode("utf-8"))
                            t0 = time.time()
                            while time.time() - t0 < 8.0:
                                raw = self.serial_conn.readline().decode("utf-8", errors="ignore").strip()
                                if not raw:
                                    continue
                                if raw.startswith("<"):
                                    match = re.search(r"<([A-Za-z]+)\|MPos:([-\d\.]+),([-\d\.]+),([-\d\.]+)", raw)
                                    if match:
                                        self.state = match.group(1)
                                        self.mpos = [float(match.group(2)), float(match.group(3)), float(match.group(4))]
                                elif raw.startswith("ok") or raw.startswith("error:"):
                                    if raw.startswith("error:"):
                                        self._log(f"Machine {raw} on line: '{line_clean}'")
                                    break
                        elif self.conn_type == "telnet" and self.socket_conn:
                            self.socket_conn.sendall((line_clean + "\n").encode("utf-8"))
                            t0 = time.time()
                            buf = ""
                            while time.time() - t0 < 8.0:
                                chunk = self.socket_conn.recv(512).decode("utf-8", errors="ignore")
                                if chunk:
                                    buf += chunk
                                    if "ok\n" in buf or buf.endswith("ok") or "error:" in buf:
                                        break
                    except Exception as e:
                        self._log(f"Stream error: {e}")
                        break

        # Wait until machine returns to Idle
        t0 = time.time()
        while time.time() - t0 < 45.0:
            if not self.connected:
                break
            with self.lock:
                try:
                    if self.conn_type == "serial" and self.serial_conn:
                        self.serial_conn.write(b"?\n")
                        time.sleep(0.04)
                        raw = self.serial_conn.read_all().decode("utf-8", errors="ignore")
                        match = re.search(r"<([A-Za-z]+)\|", raw)
                        if match and match.group(1) == "Idle":
                            self.state = "Idle"
                            break
                except Exception as e:
                    self._log(f"Connection lost in poller: {e}")
                    self._disconnect_unlocked()
            time.sleep(0.3)

        elapsed = time.time() - self.draw_start_time
        self.last_job_duration = elapsed
        self.current_job_active = False
        self.current_job_progress = 100.0
        self._log(f"{job_title} completed in {elapsed:.1f}s")

    def execute_clock_cycle(self, dt=None):
        """Spawns thread to stream clock G-code."""
        if dt is None:
            dt = datetime.datetime.now()
        time_str = dt.strftime("%H:%M")
        lines, _, _, _ = generate_clock_gcode_and_paths(dt, self.config)
        self.stream_gcode_lines(lines, f"Clock {time_str}")

    def lift_pen_and_park(self):
        z_up = self.config.get("z_up", -0.5)
        park_x, park_y = get_closest_corner(self.mpos[0], self.mpos[1])
        self._log(f"Lifting pen and parking at closest corner ({park_x:.1f}, {park_y:.1f})...")
        self.send_command(f"G0 Z{z_up:.2f} F1000")
        self.send_command(f"G0 X{park_x:.2f} Y{park_y:.2f} F12000")

    def get_status_payload(self):
        now = datetime.datetime.now()
        sec_to_next = 60.0 - now.second - (now.microsecond / 1_000_000.0)

        # Evaporation window calculation
        # If job finished at e.g. 15s into minute, evaporation is from 15s to 60s
        time_since_draw = now.second + (now.microsecond / 1_000_000.0)
        evap_pct = max(0.0, min(100.0, (time_since_draw / 60.0) * 100.0))

        return {
            "connected": self.connected,
            "conn_type": self.conn_type,
            "conn_address": self.conn_address,
            "machine_state": self.state,
            "mpos": self.mpos,
            "clock_active": self.clock_active,
            "current_job_active": self.current_job_active,
            "current_job_progress": round(self.current_job_progress, 1),
            "current_job_line": self.current_job_line,
            "next_draw_sec": round(sec_to_next, 1),
            "evap_pct": round(evap_pct, 1),
            "current_time_str": now.strftime("%H:%M:%S"),
            "last_job_duration": round(self.last_job_duration, 1),
            "last_drawn_time": self.last_drawn_time,
            "config": self.config,
            "logs": self.logs[-25:]
        }


# Global Manager Instance
manager = MachineManager()
app = bottle.Bottle()


# ---------------------------------------------------------------------------
# REST API Endpoints
# ---------------------------------------------------------------------------
@app.route("/api/status")
def api_status():
    bottle.response.content_type = "application/json"
    return manager.get_status_payload()

@app.route("/api/ports")
def api_ports():
    bottle.response.content_type = "application/json"
    return {"ports": manager.get_ports()}

@app.route("/api/connect", method="POST")
def api_connect():
    data = bottle.request.json or {}
    target = data.get("target", "auto")
    baud = int(data.get("baud", 115200))

    if target == "auto":
        ports = manager.get_ports()
        if not ports:
            return {"success": False, "error": "No serial ports found"}
        target = ports[0]["device"]

    success, msg = manager.connect(target, baud)
    return {"success": success, "message": msg}

@app.route("/api/disconnect", method="POST")
def api_disconnect():
    manager.disconnect()
    return {"success": True}

@app.route("/api/clock/toggle", method="POST")
def api_clock_toggle():
    manager.clock_active = not manager.clock_active
    manager._log(f"Clock mode {'ACTIVATED' if manager.clock_active else 'PAUSED'}")
    return {"clock_active": manager.clock_active}

@app.route("/api/clock/trigger", method="POST")
def api_clock_trigger():
    if not manager.connected:
        return {"success": False, "error": "Machine not connected"}
    manager.execute_clock_cycle()
    return {"success": True}

@app.route("/api/clock/config", method="POST")
def api_clock_config():
    data = bottle.request.json or {}
    manager.config.update(data)
    manager._log("Clock configuration updated.")
    return {"success": True, "config": manager.config}

@app.route("/api/command", method="POST")
def api_command():
    data = bottle.request.json or {}
    cmd = data.get("command", "")
    if not cmd:
        return {"success": False, "error": "Empty command"}
    resp = manager.send_command(cmd)
    return {"success": True, "response": resp}

@app.route("/api/jog", method="POST")
def api_jog():
    if not manager.connected:
        return {"success": False, "error": "Machine not connected"}
    data = bottle.request.json or {}
    cmd = data.get("command", "")
    if not cmd:
        axis = data.get("axis", "X").upper()
        dist = float(data.get("dist", 10.0))
        if axis in ("X", "Y"):
            feed = int(data.get("feed", manager.config.get("jog_feed_xy", 10000)))
            cmd = f"$J=G91 G21 {axis}{dist:.2f} F{feed}"
        elif axis == "Z":
            feed = int(data.get("feed", manager.config.get("jog_feed_z", 1000)))
            feed = min(feed, 1000)
            cmd = f"$J=G91 G21 Z{dist:.2f} F{feed}"
        else:
            return {"success": False, "error": f"Invalid axis: {axis}"}
    resp = manager.send_command(cmd)
    return {"success": True, "response": resp}

@app.route("/api/park", method="POST")
def api_park():
    if not manager.connected:
        return {"success": False, "error": "Machine not connected"}
    manager.lift_pen_and_park()
    return {"success": True}

@app.route("/api/preview")
def api_preview():
    bottle.response.content_type = "application/json"
    now = datetime.datetime.now()
    lines, strokes, dist, est = generate_clock_gcode_and_paths(now, manager.config)
    return {
        "time": now.strftime("%H:%M"),
        "strokes": strokes,
        "total_dist_mm": round(dist, 1),
        "est_sec": round(est, 1),
        "gcode_sample": lines[:40]
    }

@app.route("/api/plot", method="POST")
def api_plot():
    bottle.response.content_type = "application/json"
    if not manager.connected:
        return {"success": False, "error": "Machine not connected"}
    data = bottle.request.json or {}
    lines = data.get("lines", [])
    title = data.get("title", "Custom Art Plot")
    if not lines:
        return {"success": False, "error": "No G-code lines provided"}
    ok, msg = manager.stream_gcode_lines(lines, title)
    return {"success": ok, "message": msg}


# ---------------------------------------------------------------------------
# HTML5 Web UI Frontend
# ---------------------------------------------------------------------------
HTML_PAGE = """<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Spirit Autonomous Multi-Mode Studio</title>
  <style>
    :root {
      --bg: #0d1117;
      --card: #161b22;
      --border: #30363d;
      --accent: #58a6ff;
      --accent-glow: rgba(88, 166, 255, 0.25);
      --water: #38bdf8;
      --success: #3fb950;
      --warn: #d29922;
      --danger: #f85149;
      --purple: #bc8cff;
      --text: #c9d1d9;
      --text-muted: #8b949e;
      --text-bright: #f0f6fc;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; }
    body { background: var(--bg); color: var(--text); min-height: 100vh; display: flex; flex-direction: column; }
    
    header {
      background: var(--card); border-bottom: 1px solid var(--border); padding: 12px 24px;
      display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 12px;
    }
    .brand { display: flex; align-items: center; gap: 12px; }
    .brand h1 { font-size: 1.25rem; font-weight: 700; color: var(--text-bright); display: flex; align-items: center; gap: 8px; }
    .brand span { font-size: 0.8rem; background: rgba(56, 189, 248, 0.15); color: var(--water); padding: 3px 8px; border-radius: 12px; border: 1px solid rgba(56, 189, 248, 0.3); }

    /* Nav Tabs */
    .nav-tabs { display: flex; gap: 6px; background: #090d13; padding: 4px; border-radius: 8px; border: 1px solid var(--border); }
    .nav-tab {
      padding: 8px 16px; font-size: 0.85rem; font-weight: 600; color: var(--text-muted); cursor: pointer;
      border-radius: 6px; border: none; background: transparent; transition: all 0.2s; display: flex; align-items: center; gap: 6px;
    }
    .nav-tab:hover { color: var(--text-bright); background: rgba(255,255,255,0.05); }
    .nav-tab.active { background: var(--accent); color: #fff; box-shadow: 0 0 10px var(--accent-glow); }

    .header-right { display: flex; align-items: center; gap: 16px; }
    .live-clock { font-size: 1.3rem; font-weight: 700; color: var(--water); font-family: monospace; }

    .status-badge {
      display: inline-flex; align-items: center; gap: 6px; padding: 6px 12px; border-radius: 20px;
      font-size: 0.85rem; font-weight: 600; text-transform: uppercase; background: #21262d; border: 1px solid var(--border); color: var(--text-muted);
    }
    .status-badge.idle { background: rgba(63, 185, 80, 0.15); color: var(--success); border-color: rgba(63, 185, 80, 0.4); }
    .status-badge.run { background: rgba(56, 189, 248, 0.15); color: var(--water); border-color: rgba(56, 189, 248, 0.4); }
    .status-badge.alarm { background: rgba(248, 81, 73, 0.15); color: var(--danger); border-color: rgba(248, 81, 73, 0.4); }
    .status-dot { width: 8px; height: 8px; border-radius: 50%; background: currentColor; }

    .container {
      flex: 1; display: grid; grid-template-columns: 1fr 400px; gap: 20px; padding: 20px; max-width: 1650px; margin: 0 auto; width: 100%;
    }
    @media (max-width: 1080px) { .container { grid-template-columns: 1fr; } }

    .card { background: var(--card); border: 1px solid var(--border); border-radius: 10px; padding: 18px; margin-bottom: 20px; }
    .card-title { font-size: 0.95rem; font-weight: 600; color: var(--text-bright); margin-bottom: 14px; display: flex; align-items: center; justify-content: space-between; }

    /* Canvas */
    .canvas-wrapper {
      position: relative; width: 100%; height: 420px; background: #06090e;
      border: 1px solid var(--border); border-radius: 8px; overflow: hidden;
    }
    canvas { width: 100%; height: 100%; display: block; }
    .canvas-overlay {
      position: absolute; top: 12px; left: 12px; font-size: 0.75rem; color: var(--text-muted);
      background: rgba(13, 17, 23, 0.75); padding: 4px 8px; border-radius: 4px; pointer-events: none;
    }

    /* Buttons */
    .btn {
      display: inline-flex; align-items: center; justify-content: center; gap: 8px; padding: 10px 18px;
      border-radius: 6px; font-size: 0.9rem; font-weight: 600; cursor: pointer; border: 1px solid transparent; transition: all 0.2s;
    }
    .btn-primary { background: var(--accent); color: #fff; }
    .btn-primary:hover { background: #4a94e8; box-shadow: 0 0 12px var(--accent-glow); }
    .btn-water { background: var(--water); color: #0d1117; font-weight: 700; }
    .btn-water:hover { background: #7dd3fc; box-shadow: 0 0 12px rgba(56, 189, 248, 0.4); }
    .btn-success { background: var(--success); color: #fff; }
    .btn-danger { background: var(--danger); color: #fff; }
    .btn-outline { background: transparent; border-color: var(--border); color: var(--text); }
    .btn-outline:hover { background: #21262d; border-color: var(--text-muted); }
    .btn-sm { padding: 6px 12px; font-size: 0.8rem; }
    .btn-grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 10px; margin-top: 10px; }

    /* Forms */
    .form-group { margin-bottom: 12px; }
    .form-group label { display: block; font-size: 0.8rem; color: var(--text-muted); margin-bottom: 5px; font-weight: 500; }
    .form-row { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
    input, select, textarea {
      width: 100%; padding: 8px 12px; background: #0d1117; border: 1px solid var(--border);
      border-radius: 6px; color: var(--text-bright); font-size: 0.85rem;
    }
    input:focus, select:focus, textarea:focus { outline: none; border-color: var(--accent); }

    /* Tabs Content */
    .tab-content { display: none; }
    .tab-content.active { display: block; }

    /* Style Pills */
    .style-selector { display: grid; grid-template-columns: repeat(4, 1fr); gap: 8px; margin-bottom: 14px; }
    .style-btn {
      padding: 8px 4px; text-align: center; border-radius: 6px; border: 1px solid var(--border);
      background: #0d1117; color: var(--text-muted); font-size: 0.75rem; font-weight: 600; cursor: pointer;
    }
    .style-btn.active { border-color: var(--accent); color: var(--accent); background: rgba(88, 166, 255, 0.1); }

    /* Meters */
    .meter-wrap { margin-bottom: 14px; }
    .meter-header { display: flex; justify-content: space-between; font-size: 0.75rem; margin-bottom: 4px; color: var(--text-muted); }
    .meter-bar { height: 6px; background: #21262d; border-radius: 3px; overflow: hidden; }
    .meter-fill { height: 100%; background: var(--water); width: 0%; transition: width 0.3s; }
    .meter-fill.evap { background: linear-gradient(90deg, #38bdf8 0%, #818cf8 100%); }

    /* Presets Chips */
    .chips-wrap { display: flex; flex-wrap: wrap; gap: 6px; margin: 10px 0; }
    .chip {
      padding: 5px 10px; border-radius: 14px; background: #21262d; border: 1px solid var(--border);
      color: var(--text); font-size: 0.75rem; cursor: pointer; transition: all 0.2s;
    }
    .chip:hover { border-color: var(--water); color: var(--water); background: rgba(56, 189, 248, 0.1); }

    /* Terminal */
    .terminal {
      background: #06090e; border: 1px solid var(--border); border-radius: 6px;
      padding: 10px; height: 140px; overflow-y: auto; font-family: monospace; font-size: 0.75rem; color: #7ee787;
    }
    .terminal p { margin: 2px 0; word-break: break-all; }
  </style>
</head>
<body>

  <header>
    <div class="brand">
      <h1>💧 Spirit <span>Studio</span></h1>
    </div>

    <!-- Mode Selector Tabs -->
    <div class="nav-tabs">
      <button class="nav-tab active" onclick="switchTab('clock')">🕒 Water Clock</button>
      <button class="nav-tab" onclick="switchTab('voice')">✍️ Type on Canvas & Art</button>
      <button class="nav-tab" onclick="switchTab('mandala')">🌀 Mandalas</button>
      <button class="nav-tab" onclick="switchTab('flowfield')">🌊 Line Art & Flow</button>
    </div>

    <div class="header-right">
      <div class="live-clock" id="wallClock">--:--:--</div>
      <div class="status-badge" id="machineBadge"><div class="status-dot"></div> <span id="machineState">Connecting...</span></div>
    </div>
  </header>

  <div class="container">
    <!-- Left Column: Visualizer & Real-time Progress -->
    <div>
      <!-- 2D Slate Bed Visualizer -->
      <div class="card" style="padding:14px;">
        <div class="card-title" style="margin-bottom:10px;">
          <span id="canvasTitle">Spirit Bed View (730 x 300 mm CoreXY)</span>
          <div style="font-size:0.8rem; color:var(--text-muted); display:flex; gap:12px; align-items:center;">
            <span id="canvasCursorPos" style="color:var(--water); font-family:monospace; display:none;"></span>
            <span id="coordDisplay">X: -350.0 | Y: 150.0 | Z: -0.5</span>
          </div>
        </div>
        <div class="canvas-wrapper">
          <div class="canvas-overlay" id="canvasOverlayTag">Live Water Slate (-730 to 0 X, 0 to 300 Y) [🪞 Mirrored]</div>
          <canvas id="bedCanvas"></canvas>
        </div>

        <!-- Live Toolpath Optimizer & Metrics Banner -->
        <div style="margin-top:10px; display:flex; justify-content:space-between; align-items:center; background:#090d13; padding:8px 12px; border-radius:6px; border:1px solid var(--border); font-size:0.75rem; color:var(--text-muted); font-family:monospace;">
          <div id="pathMetricsBanner">⚡ Toolpath Optimizer: Ready</div>
          <label style="display:flex; align-items:center; gap:6px; cursor:pointer; color:var(--water); font-size:0.75rem;">
            <input type="checkbox" id="chkOptimizePaths" checked onchange="onGeometryChange()" style="accent-color:var(--water);">
            <span>Optimize Toolpath</span>
          </label>
        </div>

        <div style="margin-top:14px; display:grid; grid-template-columns:1fr 1fr; gap:16px;">
          <div class="meter-wrap">
            <div class="meter-header">
              <span>Streaming Progress</span>
              <span id="jobProgressText">Idle</span>
            </div>
            <div class="meter-bar"><div class="meter-fill" id="jobProgressBar"></div></div>
          </div>
          <div class="meter-wrap">
            <div class="meter-header">
              <span>Water Evaporation Window</span>
              <span id="evapText">100% (Dry)</span>
            </div>
            <div class="meter-bar"><div class="meter-fill evap" id="evapBar"></div></div>
          </div>
        </div>
      </div>

      <!-- FluidNC Serial / WebSocket Terminal -->
      <div class="card" style="padding:14px;">
        <div class="card-title" style="margin-bottom:8px;">
          <span>FluidNC WebSocket Telemetry</span>
          <button class="btn btn-outline btn-sm" onclick="sendWs('?')">Query (?)</button>
        </div>
        <div class="terminal" id="termLogs"></div>
        <div style="display:flex; gap:8px; margin-top:8px;">
          <input type="text" id="customGcode" placeholder="Enter G-Code ($H, ?, G0 Z-0.5, etc.)" onkeydown="if(event.key==='Enter') sendManualGcode()">
          <button class="btn btn-outline btn-sm" onclick="sendManualGcode()">Send</button>
        </div>
      </div>
    </div>

    <!-- Right Column: Mode Panels -->
    <div>

      <!-- ============================================================== -->
      <!-- TAB 1: 🕒 WATER CLOCK MODE -->
      <!-- ============================================================== -->
      <div id="tab-clock" class="tab-content active">
        <div class="card">
          <div class="card-title">Continuous 1-Minute Clock</div>
          <div style="display:flex; align-items:center; justify-content:space-between; margin-bottom:12px; background:#0d1117; padding:12px; border-radius:8px; border:1px solid var(--border);">
            <div>
              <div style="font-size:0.75rem; color:var(--text-muted); text-transform:uppercase;">Next Plot In</div>
              <div style="font-size:1.5rem; font-weight:700; color:var(--water); font-family:monospace;" id="countdownTimer">--s</div>
            </div>
            <button class="btn btn-primary" id="btnClockToggle" onclick="toggleClock()">▶ Start Clock</button>
          </div>

          <label style="font-size:0.8rem; color:var(--text-muted); margin-bottom:6px; display:block;">Clock Style</label>
          <div class="style-selector">
            <div class="style-btn active" id="style_circle" onclick="setStyle('circle')">Circle</div>
            <div class="style-btn" id="style_square" onclick="setStyle('square')">Simple Square</div>
            <div class="style-btn" id="style_minimal" onclick="setStyle('minimal')">Minimal</div>
            <div class="style-btn" id="style_digits" onclick="setStyle('digits')">Large Digits</div>
          </div>
          <p id="styleHint" style="font-size:0.75rem; color:var(--water); margin-bottom:12px;">Full dial with outer arc, 12 ticks, and analog hands (pure analog, clean & elegant).</p>

          <div style="display:flex; gap:8px; margin-bottom:10px;">
            <button class="btn btn-water btn-sm" style="flex:2;" onclick="triggerDrawNow()">⚡ Draw Clock Now</button>
            <button class="btn btn-outline btn-sm" style="flex:1;" onclick="downloadCurrentGcode('spirit_clock.gcode')" title="Export .gcode file">💾 Save .gcode</button>
          </div>
        </div>
      </div>

      <!-- ============================================================== -->
      <!-- TAB 2: ✍️ CANVAS TEXT & ART MODE -->
      <!-- ============================================================== -->
      <div id="tab-voice" class="tab-content">
        <div class="card">
          <div class="card-title">
            <span>✍️ Canvas Text & Art Studio</span>
            <span style="font-size:0.75rem; color:var(--water);">Click canvas to place text</span>
          </div>

          <!-- Sub-mode Toggle -->
          <div style="display:flex; gap:8px; margin-bottom:14px;">
            <button class="btn btn-primary btn-sm" id="btnSubText" style="flex:1;" onclick="setArtSubMode('text')">✍️ Type on Canvas</button>
            <button class="btn btn-outline btn-sm" id="btnSubArt" style="flex:1;" onclick="setArtSubMode('art')">🎨 Line Art Presets</button>
          </div>

          <!-- Sub-panel 1: TYPE ON CANVAS -->
          <div id="panelSubText">
            <div class="form-group">
              <label>Type Anything to Draw on Canvas:</label>
              <textarea id="inpCanvasText" rows="3" placeholder="Type text here... (Multi-line supported)&#10;e.g. Hello FabLab!&#10;Spirit 2026" oninput="generateCanvasText()">HELLO FABLAB</textarea>
            </div>

            <div class="form-row">
              <div class="form-group">
                <label>Text Height (mm)</label>
                <input type="number" id="inpTextSize" value="28" min="10" max="90" step="2" oninput="generateCanvasText()">
              </div>
              <div class="form-group">
                <label>Alignment</label>
                <select id="selTextAlign" onchange="generateCanvasText()">
                  <option value="center" selected>Center</option>
                  <option value="left">Left</option>
                  <option value="right">Right</option>
                </select>
              </div>
            </div>

            <div class="form-row">
              <div class="form-group">
                <label>Position X (mm)</label>
                <input type="number" id="inpCanvasTextX" value="-350" step="5" oninput="generateCanvasText()">
              </div>
              <div class="form-group">
                <label>Position Y (mm)</label>
                <input type="number" id="inpCanvasTextY" value="150" step="5" oninput="generateCanvasText()">
              </div>
            </div>

            <div style="font-size:0.75rem; color:var(--text-muted); margin-bottom:12px; background:rgba(56,189,248,0.08); padding:6px 10px; border-radius:6px; border:1px solid rgba(56,189,248,0.2);">
              🖱️ <em>Tip: Click or drag anywhere on the bed canvas to place your text!</em>
            </div>

            <label style="font-size:0.8rem; color:var(--text-muted);">Quick Inserts:</label>
            <div class="chips-wrap">
              <div class="chip" onclick="setCanvasTextPrompt('HELLO WORLD')">👋 Hello World</div>
              <div class="chip" onclick="setCanvasTextPrompt('SPIRIT 2026')">💧 Spirit 2026</div>
              <div class="chip" onclick="setCanvasTextPrompt('FABLAB')">🔬 FabLab</div>
              <div class="chip" onclick="setCanvasTextPrompt('WATER CLOCK')">🕒 Water Clock</div>
              <div class="chip" onclick="setCanvasTextPrompt('MAKE ART')">✨ Make Art</div>
            </div>

            <div style="display:flex; gap:8px; margin-top:14px;">
              <button class="btn btn-outline btn-sm" onclick="centerCanvasText()" style="flex:1;">🎯 Center</button>
              <button class="btn btn-outline btn-sm" onclick="downloadCurrentGcode('spirit_text.gcode')" style="flex:1;" title="Export .gcode file">💾 Save .gcode</button>
              <button class="btn btn-water btn-sm" style="flex:2;" onclick="plotCurrentVectorStrokes()">🚀 Plot Text</button>
            </div>
          </div>

          <!-- Sub-panel 2: LINE ART PRESETS & SPEECH -->
          <div id="panelSubArt" style="display:none;">
            <div class="form-group">
              <label>Say or Type What to Draw:</label>
              <div style="display:flex; gap:8px;">
                <input type="text" id="inpVoicePrompt" placeholder="e.g. origami bird, cat, mountain, flower..." onkeydown="if(event.key==='Enter') generateVoiceArt()">
                <button class="btn btn-primary btn-sm" id="btnMic" onclick="toggleVoiceRecognition()" title="Click to Speak">🎙️ Speak</button>
              </div>
              <div id="voiceStatus" style="font-size:0.75rem; color:var(--text-muted); margin-top:4px;">Click 🎙️ to talk or choose a preset below:</div>
            </div>

            <label style="font-size:0.8rem; color:var(--text-muted);">Quick Presets:</label>
            <div class="chips-wrap">
              <div class="chip" onclick="setAndGenPrompt('origami bird')">🐦 Origami Bird</div>
              <div class="chip" onclick="setAndGenPrompt('cat')">🐱 Cat</div>
              <div class="chip" onclick="setAndGenPrompt('lotus flower')">🌸 Lotus Flower</div>
              <div class="chip" onclick="setAndGenPrompt('mountain range')">⛰️ Mountains</div>
              <div class="chip" onclick="setAndGenPrompt('butterfly')">🦋 Butterfly</div>
              <div class="chip" onclick="setAndGenPrompt('star')">⭐ Star</div>
              <div class="chip" onclick="setAndGenPrompt('heart')">❤️ Heart</div>
              <div class="chip" onclick="setAndGenPrompt('sailboat')">⛵ Sailboat</div>
              <div class="chip" onclick="setAndGenPrompt('infinity')">♾️ Infinity Loop</div>
            </div>

            <div style="display:flex; gap:8px; margin-top:14px;">
              <button class="btn btn-outline btn-sm" style="flex:1;" onclick="generateVoiceArt()">👁️ Preview</button>
              <button class="btn btn-outline btn-sm" style="flex:1;" onclick="downloadCurrentGcode('spirit_art.gcode')" title="Export .gcode file">💾 Save .gcode</button>
              <button class="btn btn-water btn-sm" style="flex:1;" onclick="plotCurrentVectorStrokes()">🚀 Plot Art</button>
            </div>
          </div>
        </div>
      </div>

      <!-- ============================================================== -->
      <!-- TAB 3: 🌀 PATTERNS & MANDALAS MODE -->
      <!-- ============================================================== -->
      <div id="tab-mandala" class="tab-content">
        <div class="card">
          <div class="card-title">Parametric Sacred Geometry & Mandalas</div>

          <div class="form-group">
            <label>Mandala Algorithm</label>
            <select id="selMandalaType" onchange="generateMandala()">
              <option value="lotus">Lotus Petal Mandala</option>
              <option value="spiro">Hypotrochoid / Spirograph</option>
              <option value="harmonograph">Harmonograph Curves</option>
              <option value="flower">Flower of Life Grid</option>
            </select>
          </div>

          <div class="form-row">
            <div class="form-group">
              <label>Symmetry / Petals</label>
              <input type="number" id="inpMandalaSymmetry" value="8" min="3" max="24" onchange="generateMandala()">
            </div>
            <div class="form-group">
              <label>Harmonics / Waves</label>
              <input type="number" id="inpMandalaFreq" value="5" min="1" max="15" onchange="generateMandala()">
            </div>
          </div>

          <div class="form-row">
            <div class="form-group">
              <label>Radius (mm)</label>
              <input type="number" id="inpMandalaRadius" value="105" min="30" max="135" onchange="generateMandala()">
            </div>
            <div class="form-group">
              <label>Complexity / Layers</label>
              <input type="number" id="inpMandalaLayers" value="3" min="1" max="6" onchange="generateMandala()">
            </div>
          </div>

          <div style="display:flex; gap:8px; margin-top:10px;">
            <button class="btn btn-outline btn-sm" style="flex:1;" onclick="randomizeMandala()">🎲 Random</button>
            <button class="btn btn-outline btn-sm" style="flex:1;" onclick="downloadCurrentGcode('spirit_mandala.gcode')" title="Export .gcode file">💾 Save .gcode</button>
            <button class="btn btn-water btn-sm" style="flex:1;" onclick="plotCurrentVectorStrokes()">🚀 Plot</button>
          </div>
        </div>
      </div>

      <!-- ============================================================== -->
      <!-- TAB 4: 🌊 LINE ART & FLOW FIELDS MODE -->
      <!-- ============================================================== -->
      <div id="tab-flowfield" class="tab-content">
        <div class="card">
          <div class="card-title">Flow Field & Generative Line Art</div>

          <div class="form-group">
            <label>Algorithm</label>
            <select id="selFlowType" onchange="generateFlowArt()">
              <option value="perlin">Perlin Noise Fluid Streams</option>
              <option value="vortex">Dual Vortex Currents</option>
              <option value="waves">Sine Wave Topography</option>
              <option value="crystals">Crystalline Angular Mesh</option>
            </select>
          </div>

          <div class="form-row">
            <div class="form-group">
              <label>Stream Count</label>
              <input type="number" id="inpFlowLines" value="28" min="8" max="70" onchange="generateFlowArt()">
            </div>
            <div class="form-group">
              <label>Steps / Length</label>
              <input type="number" id="inpFlowSteps" value="55" min="20" max="120" onchange="generateFlowArt()">
            </div>
          </div>

          <div class="form-row">
            <div class="form-group">
              <label>Curliness / Scale</label>
              <input type="number" id="inpFlowCurl" value="18" min="5" max="60" onchange="generateFlowArt()">
            </div>
            <div class="form-group">
              <label>Random Seed</label>
              <input type="number" id="inpFlowSeed" value="42" onchange="generateFlowArt()">
            </div>
          </div>

          <div style="display:flex; gap:8px; margin-top:10px;">
            <button class="btn btn-outline btn-sm" style="flex:1;" onclick="randomizeFlowArt()">🎲 Random</button>
            <button class="btn btn-outline btn-sm" style="flex:1;" onclick="downloadCurrentGcode('spirit_flow.gcode')" title="Export .gcode file">💾 Save .gcode</button>
            <button class="btn btn-water btn-sm" style="flex:1;" onclick="plotCurrentVectorStrokes()">🚀 Plot</button>
          </div>
        </div>
      </div>

      <!-- ============================================================== -->
      <!-- SHARED MACHINE SETTINGS & HARDWARE CONTROLS -->
      <!-- ============================================================== -->
      <div class="card">
        <div class="card-title">Hardware & Safety Controls</div>
        <div class="btn-grid">
          <button class="btn btn-outline btn-sm" onclick="sendWs('$H\\n')">🏠 Home Machine ($H)</button>
          <button class="btn btn-outline btn-sm" onclick="sendWs('G0 Z-0.5 F2000\\n')">⬆ Lift Pen (Z-0.5)</button>
          <button class="btn btn-outline btn-sm" onclick="sendWs('G1 Z-3.0 F2000\\n')">⬇ Drop Pen (Z-3.0)</button>
          <button class="btn btn-outline btn-sm" onclick="sendWs('G0 X-350 Y150 F10000\\n')">🎯 Park Center</button>
          <button class="btn btn-danger btn-sm" onclick="sendWs('!\\n')">⏸ Feed Hold (!)</button>
          <button class="btn btn-success btn-sm" onclick="sendWs('~\\n')">▶ Resume (~)</button>
        </div>

        <div style="margin-top:14px; padding-top:10px; border-top:1px solid var(--border);">
          <div class="form-row">
            <div class="form-group">
              <label>Center X (mm)</label>
              <input type="number" id="inpCenterX" value="-350" step="5" onchange="onGeometryChange()">
            </div>
            <div class="form-group">
              <label>Center Y (mm)</label>
              <input type="number" id="inpCenterY" value="150" step="5" onchange="onGeometryChange()">
            </div>
          </div>

          <div class="form-row">
            <div class="form-group">
              <label>Pen-Up Z</label>
              <input type="number" id="inpZUp" value="-0.5" step="0.5">
            </div>
            <div class="form-group">
              <label>Pen-Down Z</label>
              <input type="number" id="inpZDown" value="-4.5" step="0.5">
            </div>
          </div>

          <div class="form-row">
            <div class="form-group">
              <label>Draw Speed</label>
              <input type="number" id="inpFeedXY" value="9000" step="500">
            </div>
            <div class="form-group">
              <label>Clock Radius (mm)</label>
              <input type="number" id="inpRadius" value="110" step="5" onchange="onGeometryChange()">
            </div>
          </div>

          <div class="form-group" style="margin-top:4px;">
            <label style="display:flex; align-items:center; gap:8px; cursor:pointer; color:var(--water); font-size:0.85rem; font-weight:600;">
              <input type="checkbox" id="chkMirrorX" checked onchange="onGeometryChange()" style="width:16px; height:16px; accent-color:var(--water);">
              <span>🪞 Mirror Horizontally (Viewed from back / glass)</span>
            </label>
          </div>
        </div>

        <!-- Connection -->
        <div style="margin-top:10px; padding-top:10px; border-top:1px solid var(--border); display:flex; gap:8px;">
          <input type="text" id="inpWsHost" value="" placeholder="ESP32 IP e.g. 192.168.1.150">
          <button class="btn btn-outline btn-sm" onclick="connectWs()">Connect WS:81</button>
        </div>
      </div>

    </div>
  </div>

  <script>
    // -------------------------------------------------------------------------
    // HERSHEY SINGLE-STROKE GLYPHS (Full Typography A-Z, 0-9, Symbols)
    // -------------------------------------------------------------------------
    const HERSHEY = {
      'A': [[[0, 0], [8, 20], [16, 0]], [[3, 7], [13, 7]]],
      'B': [[[0, 0], [0, 20], [11, 20], [14, 17], [14, 13], [11, 10], [0, 10]], [[11, 10], [15, 7], [15, 3], [12, 0], [0, 0]]],
      'C': [[[15, 16], [12, 19], [6, 20], [2, 16], [0, 10], [2, 4], [6, 0], [12, 1], [15, 4]]],
      'D': [[[0, 0], [0, 20], [10, 20], [14, 16], [15, 10], [14, 4], [10, 0], [0, 0]]],
      'E': [[[14, 20], [0, 20], [0, 0], [14, 0]], [[0, 10], [11, 10]]],
      'F': [[[0, 0], [0, 20], [14, 20]], [[0, 10], [11, 10]]],
      'G': [[[15, 16], [12, 19], [6, 20], [2, 16], [0, 10], [2, 4], [6, 0], [12, 1], [15, 5], [15, 10], [8, 10]]],
      'H': [[[0, 20], [0, 0]], [[14, 20], [14, 0]], [[0, 10], [14, 10]]],
      'I': [[[3, 20], [11, 20]], [[7, 20], [7, 0]], [[3, 0], [11, 0]]],
      'J': [[[12, 20], [12, 5], [10, 1], [6, 0], [2, 2], [0, 6]]],
      'K': [[[0, 20], [0, 0]], [[14, 20], [0, 7]], [[6, 12], [14, 0]]],
      'L': [[[0, 20], [0, 0], [13, 0]]],
      'M': [[[0, 0], [0, 20], [8, 7], [16, 20], [16, 0]]],
      'N': [[[0, 0], [0, 20], [14, 0], [14, 20]]],
      'O': [[[0, 10], [2, 16], [6, 20], [10, 20], [14, 16], [16, 10], [14, 4], [10, 0], [6, 0], [2, 4], [0, 10]]],
      'P': [[[0, 0], [0, 20], [11, 20], [14, 17], [14, 13], [11, 10], [0, 10]]],
      'Q': [[[0, 10], [2, 16], [6, 20], [10, 20], [14, 16], [16, 10], [14, 4], [10, 0], [6, 0], [2, 4], [0, 10]], [[9, 5], [15, -2]]],
      'R': [[[0, 0], [0, 20], [11, 20], [14, 17], [14, 13], [11, 10], [0, 10]], [[8, 10], [15, 0]]],
      'S': [[[14, 17], [11, 20], [5, 20], [1, 17], [1, 13], [4, 11], [11, 9], [14, 7], [14, 3], [10, 0], [4, 0], [0, 3]]],
      'T': [[[0, 20], [14, 20]], [[7, 20], [7, 0]]],
      'U': [[[0, 20], [0, 6], [2, 2], [6, 0], [10, 0], [14, 2], [16, 6], [16, 20]]],
      'V': [[[0, 20], [8, 0], [16, 20]]],
      'W': [[[0, 20], [4, 0], [8, 13], [12, 0], [16, 20]]],
      'X': [[[1, 20], [15, 0]], [[15, 20], [1, 0]]],
      'Y': [[[0, 20], [8, 11], [16, 20]], [[8, 11], [8, 0]]],
      'Z': [[[1, 20], [15, 20], [1, 0], [15, 0]]],
      '0': [[[5, 18], [3, 14], [3, 6], [5, 2], [8, 0], [11, 2], [13, 6], [13, 14], [11, 18], [8, 20], [5, 18]]],
      '1': [[[4, 15], [8, 20], [8, 0]]],
      '2': [[[3, 15], [4, 18], [8, 20], [12, 18], [13, 15], [13, 12], [3, 0], [14, 0]]],
      '3': [[[3, 16], [5, 19], [8, 20], [11, 19], [13, 16], [13, 12], [10, 10], [13, 8], [13, 4], [11, 1], [8, 0], [5, 1], [3, 4]]],
      '4': [[[11, 20], [3, 7], [14, 7]], [[11, 20], [11, 0]]],
      '5': [[[13, 20], [3, 20], [3, 12], [7, 13], [11, 12], [13, 9], [13, 4], [11, 1], [8, 0], [4, 1], [3, 3]]],
      '6': [[[12, 17], [9, 20], [5, 18], [3, 13], [3, 6], [5, 2], [8, 0], [11, 2], [13, 5], [13, 8], [11, 11], [8, 12], [5, 10], [3, 6]]],
      '7': [[[3, 20], [13, 20], [7, 0]]],
      '8': [[[8, 10], [5, 12], [3, 15], [5, 18], [8, 20], [11, 18], [13, 15], [11, 12], [8, 10], [5, 8], [3, 5], [5, 2], [8, 0], [11, 2], [13, 5], [11, 8], [8, 10]]],
      '9': [[[13, 14], [11, 10], [8, 8], [5, 9], [3, 12], [3, 15], [5, 18], [8, 20], [11, 18], [13, 15], [13, 6], [11, 2], [8, 0], [4, 2]]],
      ':': [[[8, 14], [8, 12]], [[8, 6], [8, 4]]],
      ';': [[[8, 14], [8, 12]], [[8, 4], [8, 2], [6, -1]]],
      '!': [[[8, 20], [8, 6]], [[8, 2], [8, 0]]],
      '?': [[[2, 16], [5, 20], [11, 20], [14, 16], [13, 12], [8, 9], [8, 6]], [[8, 2], [8, 0]]],
      '.': [[[8, 2], [8, 0]]],
      ',': [[[8, 3], [8, 1], [6, -2]]],
      '-': [[[3, 10], [13, 10]]],
      '+': [[[8, 16], [8, 4]], [[2, 10], [14, 10]]],
      '=': [[[3, 13], [13, 13]], [[3, 7], [13, 7]]],
      '/': [[[2, 0], [14, 20]]],
      '\\': [[[2, 20], [14, 0]]],
      '(': [[[11, 20], [7, 16], [5, 10], [7, 4], [11, 0]]],
      ')': [[[5, 20], [9, 16], [11, 10], [9, 4], [5, 0]]],
      '[': [[[11, 20], [5, 20], [5, 0], [11, 0]]],
      ']': [[[5, 20], [11, 20], [11, 0], [5, 0]]],
      '\'': [[[8, 20], [8, 14]]],
      '\"': [[[5, 20], [5, 14]], [[11, 20], [11, 14]]],
      '&': [[[14, 4], [14, 2], [11, 0], [6, 0], [2, 3], [2, 8], [6, 12], [10, 16], [10, 18], [8, 20], [5, 20], [3, 18], [3, 15], [7, 10], [14, 0]]],
      '#': [[[5, 22], [3, -2]], [[11, 22], [9, -2]], [[1, 14], [15, 14]], [[0, 6], [14, 6]]],
      '@': [[[14, 5], [12, 1], [7, 0], [2, 4], [0, 10], [2, 16], [7, 20], [12, 19], [15, 14], [15, 7], [13, 5], [10, 5], [8, 7], [8, 12], [10, 14], [12, 13], [12, 5]]],
      ' ': []
    };

    // Initialize lowercase aliases for HERSHEY
    for (let c = 65; c <= 90; c++) {
      const upper = String.fromCharCode(c);
      const lower = String.fromCharCode(c + 32);
      if (!HERSHEY[lower] && HERSHEY[upper]) {
        HERSHEY[lower] = HERSHEY[upper];
      }
    }

    // Vector art database for instant voice/text sketches (relative 100x100 coordinates)
    const VECTOR_LIBRARY = {
      'origami bird': [
        [[10, 80], [45, 85], [90, 20], [50, 45], [10, 80]],
        [[50, 45], [45, 85]],
        [[50, 45], [75, 55], [90, 20]],
        [[45, 85], [30, 95], [10, 80]]
      ],
      'bird': [
        [[15, 60], [35, 40], [55, 60], [75, 40], [95, 60]],
        [[35, 40], [55, 75], [75, 40]]
      ],
      'cat': [
        [[30, 75], [30, 40], [20, 20], [40, 30], [60, 30], [80, 20], [70, 40], [70, 75], [30, 75]],
        [[35, 50], [45, 50]], [[55, 50], [65, 50]],
        [[50, 58], [50, 65]], [[50, 65], [40, 68]], [[50, 65], [60, 68]],
        [[70, 65], [90, 70], [92, 50], [85, 40]]
      ],
      'lotus flower': [
        [[50, 85], [35, 60], [50, 20], [65, 60], [50, 85]],
        [[50, 85], [20, 70], [25, 40], [40, 55]],
        [[50, 85], [80, 70], [75, 40], [60, 55]],
        [[15, 85], [85, 85]]
      ],
      'mountain range': [
        [[10, 80], [35, 30], [55, 65], [75, 20], [95, 80]],
        [[35, 30], [30, 45], [42, 50]],
        [[75, 20], [68, 38], [82, 45]]
      ],
      'butterfly': [
        [[50, 25], [50, 80]],
        [[50, 30], [80, 15], [90, 45], [65, 55], [50, 50]],
        [[50, 50], [75, 60], [70, 80], [50, 70]],
        [[50, 30], [20, 15], [10, 45], [35, 55], [50, 50]],
        [[50, 50], [25, 60], [30, 80], [50, 70]],
        [[50, 25], [40, 15]], [[50, 25], [60, 15]]
      ],
      'star': [
        [[50, 10], [62, 38], [92, 38], [68, 56], [77, 85], [50, 68], [23, 85], [32, 56], [8, 38], [38, 38], [50, 10]]
      ],
      'heart': [
        [[50, 85], [20, 55], [15, 35], [25, 20], [40, 20], [50, 32], [60, 20], [75, 20], [85, 35], [80, 55], [50, 85]]
      ],
      'sailboat': [
        [[20, 75], [80, 75], [70, 90], [30, 90], [20, 75]],
        [[50, 75], [50, 20], [80, 65], [50, 65]],
        [[45, 70], [45, 28], [25, 65], [45, 65]]
      ],
      'infinity': [
        Array.from({length: 65}, (_, i) => {
          const t = (i * 2 * Math.PI) / 64;
          const scale = 38;
          return [50 + scale * Math.cos(t), 50 + scale * Math.sin(t) * Math.cos(t) * 0.8];
        })
      ]
    };

    // State Variables
    let currentTab = "clock";
    let artSubMode = "text"; // "text" or "art"
    let isDraggingText = false;
    let ws = null;
    let wsConnected = false;
    let machineState = "Disconnected";
    let toolPos = [-350, 150, -0.5];
    let clockActive = false;
    let clockStyle = "circle";
    let isDrawingJob = false;
    let lastDrawnMinute = -1;
    let canvas, ctx;

    // Current Vector Strokes ready to plot [[(x,y), ...], ...]
    let currentStrokes = [];

    // -------------------------------------------------------------------------
    // INITIALIZATION & TAB SWITCHING
    // -------------------------------------------------------------------------
    window.onload = function() {
      canvas = document.getElementById("bedCanvas");
      ctx = canvas.getContext("2d");
      resizeCanvas();
      window.addEventListener("resize", resizeCanvas);
      setupCanvasInteraction();

      // Check if served from Python Server (:8080) or ESP32 WebUI
      checkServerBackend().then(hasServer => {
        if (!hasServer) {
          const curHost = window.location.hostname || "192.168.1.150";
          document.getElementById("inpWsHost").value = curHost;
          connectWs();
        }
      });

      generateClockPreview();
      updatePathMetrics(currentStrokes);

      setInterval(tickClockLoop, 250);
      setInterval(() => {
        if (isServerBackend) pollServerStatus();
        else pollStatus();
      }, 400);
      requestAnimationFrame(renderCanvas);
    };

    function resizeCanvas() {
      const rect = canvas.getBoundingClientRect();
      canvas.width = rect.width * window.devicePixelRatio;
      canvas.height = rect.height * window.devicePixelRatio;
    }

    function switchTab(tabId) {
      currentTab = tabId;
      document.querySelectorAll(".nav-tab").forEach(b => b.classList.remove("active"));
      document.querySelectorAll(".tab-content").forEach(c => c.classList.remove("active"));

      const activeBtn = Array.from(document.querySelectorAll(".nav-tab")).find(b => b.innerText.toLowerCase().includes(tabId));
      if (activeBtn) activeBtn.classList.add("active");

      const panel = document.getElementById(`tab-${tabId}`);
      if (panel) panel.classList.add("active");

      // Generate preview for selected mode
      if (tabId === "clock") generateClockPreview();
      else if (tabId === "voice") {
        if (artSubMode === "text") generateCanvasText();
        else generateVoiceArt();
      }
      else if (tabId === "mandala") generateMandala();
      else if (tabId === "flowfield") generateFlowArt();
      updatePathMetrics(currentStrokes);
    }

    function onGeometryChange() {
      if (currentTab === "clock") generateClockPreview();
      else if (currentTab === "voice") {
        if (artSubMode === "text") generateCanvasText();
        else generateVoiceArt();
      }
      else if (currentTab === "mandala") generateMandala();
      else if (currentTab === "flowfield") generateFlowArt();
      updatePathMetrics(currentStrokes);
      syncServerConfig();
    }

    // -------------------------------------------------------------------------
    // COMMUNICATION: FLUIDNC WEBSOCKET & PYTHON SERVER API
    // -------------------------------------------------------------------------
    let isServerBackend = false;

    async function checkServerBackend() {
      try {
        const res = await fetch("/api/status");
        if (res.ok) {
          const data = await res.json();
          isServerBackend = true;
          logTerm(`Connected to Python Server (${data.conn_type || 'Serial'} on ${data.conn_address || 'COM'})`);
          handleServerStatus(data);
          return true;
        }
      } catch (e) {}
      return false;
    }

    async function pollServerStatus() {
      if (!isServerBackend) return;
      try {
        const res = await fetch("/api/status");
        if (res.ok) {
          const data = await res.json();
          handleServerStatus(data);
        }
      } catch(e) {}
    }

    function handleServerStatus(data) {
      machineState = data.machine_state || (data.connected ? "Idle" : "Disconnected");
      if (data.mpos && data.mpos.length >= 3) {
        toolPos = data.mpos;
        document.getElementById("coordDisplay").innerText = 
          `X: ${toolPos[0].toFixed(1)} | Y: ${toolPos[1].toFixed(1)} | Z: ${toolPos[2].toFixed(1)} mm`;
      }
      updateBadge(machineState);

      // Reflect server clock state
      if (typeof data.clock_active === "boolean") {
        clockActive = data.clock_active;
        updateClockBtn();
      }

      // Reflect current streaming job progress
      if (data.current_job_active) {
        isDrawingJob = true;
        const p = data.current_job_progress || 0;
        document.getElementById("jobProgressBar").style.width = p.toFixed(0) + "%";
        document.getElementById("jobProgressText").innerText = `${p.toFixed(0)}% (${data.current_job_line || ''})`;
      } else if (isDrawingJob && !data.current_job_active) {
        isDrawingJob = false;
        document.getElementById("jobProgressBar").style.width = "100%";
        document.getElementById("jobProgressText").innerText = "Completed";
      }
    }

    function syncServerConfig() {
      if (!isServerBackend) return;
      const payload = {
        style: clockStyle,
        center_x: parseFloat(document.getElementById("inpCenterX").value),
        center_y: parseFloat(document.getElementById("inpCenterY").value),
        radius: parseFloat(document.getElementById("inpRadius").value),
        z_up: parseFloat(document.getElementById("inpZUp").value),
        z_down: parseFloat(document.getElementById("inpZDown").value),
        feed_xy: parseInt(document.getElementById("inpFeedXY").value),
        mirror_x: document.getElementById("chkMirrorX").checked
      };
      fetch("/api/clock/config", {
        method: "POST",
        headers: {"Content-Type": "application/json"},
        body: JSON.stringify(payload)
      }).catch(()=>{});
    }

    function logTerm(msg) {
      const term = document.getElementById("termLogs");
      const p = document.createElement("p");
      p.innerText = `[${new Date().toTimeString().split(" ")[0]}] ${msg}`;
      term.appendChild(p);
      if (term.childNodes.length > 50) term.removeChild(term.firstChild);
      term.scrollTop = term.scrollHeight;
    }

    function connectWs() {
      const host = document.getElementById("inpWsHost").value.trim();
      if (!host) return;

      if (ws) {
        try { ws.close(); } catch(e){}
      }

      logTerm(`Connecting to ws://${host}:81/...`);
      try {
        ws = new WebSocket(`ws://${host}:81/`);
        ws.onopen = function() {
          wsConnected = true;
          logTerm(`WebSocket connected to ${host}:81!`);
          updateBadge("Idle");
          sendWs("?\n");
        };
        ws.onclose = function() {
          wsConnected = false;
          updateBadge("Disconnected");
          logTerm("WebSocket disconnected.");
        };
        ws.onerror = function() { logTerm("WebSocket error."); };
        ws.onmessage = function(evt) {
          const raw = evt.data.trim();
          if (raw.startsWith("<")) parseStatus(raw);
          else if (raw) logTerm(`ESP32: ${raw}`);
        };
      } catch(e) { logTerm(`Error: ${e}`); }
    }

    function sendWs(cmd) {
      if (wsConnected && ws && ws.readyState === WebSocket.OPEN) {
        ws.send(cmd);
      } else if (isServerBackend) {
        fetch("/api/command", {
          method: "POST",
          headers: {"Content-Type": "application/json"},
          body: JSON.stringify({command: cmd.trim()})
        }).catch(e => logTerm(`Send error: ${e}`));
      }
    }

    function sendManualGcode() {
      const inp = document.getElementById("customGcode");
      const cmd = inp.value.trim();
      if (!cmd) return;
      sendWs(cmd + "\n");
      inp.value = "";
    }

    function parseStatus(str) {
      const match = str.match(/<([A-Za-z]+)\\|MPos:([-\\d\\.]+),([-\\d\\.]+),([-\\d\\.]+)/);
      if (match) {
        machineState = match[1];
        toolPos = [parseFloat(match[2]), parseFloat(match[3]), parseFloat(match[4])];
        updateBadge(machineState);
        document.getElementById("coordDisplay").innerText = 
          `X: ${toolPos[0].toFixed(1)} | Y: ${toolPos[1].toFixed(1)} | Z: ${toolPos[2].toFixed(1)} mm`;
      }
    }

    function updateBadge(st) {
      const isOnline = wsConnected || isServerBackend;
      const badge = document.getElementById("machineBadge");
      document.getElementById("machineState").innerText = isOnline ? st : "Disconnected";
      badge.className = "status-badge " + (isOnline ? st.toLowerCase() : "");
    }

    function pollStatus() {
      if (wsConnected) sendWs("?");
    }

    // -------------------------------------------------------------------------
    // MODE 1: CLOCK LOGIC (SIMPLIFIED SQUARE & CIRCLE)
    // -------------------------------------------------------------------------
    function updateClockBtn() {
      const btn = document.getElementById("btnClockToggle");
      if (!btn) return;
      if (clockActive) {
        btn.innerText = "⏸ Pause Clock";
        btn.className = "btn btn-danger";
      } else {
        btn.innerText = "▶ Start Clock";
        btn.className = "btn btn-primary";
      }
    }

    function setStyle(st) {
      clockStyle = st;
      document.querySelectorAll(".style-btn").forEach(el => el.classList.remove("active"));
      const btn = document.getElementById("style_" + st);
      if (btn) btn.classList.add("active");

      const hints = {
        'circle': 'Full dial with outer arc, 12 ticks, and analog hands (pure analog, clean & elegant).',
        'square': '✨ Clean Minimal Square: crisp continuous box + centered digital time (ultra fast, ~6s, zero puddles!).',
        'minimal': 'Hands & digital time only (no outer frame).',
        'digits': 'Large centered Hershey digital numbers only.'
      };
      document.getElementById("styleHint").innerText = hints[st] || '';
      generateClockPreview();
      updatePathMetrics(currentStrokes);
      syncServerConfig();
    }

    function toggleClock() {
      if (isServerBackend) {
        fetch("/api/clock/toggle", {method: "POST"})
          .then(r => r.json())
          .then(d => {
            clockActive = d.clock_active;
            updateClockBtn();
            logTerm(`Clock mode: ${clockActive ? 'ACTIVE' : 'PAUSED'}`);
          });
      } else {
        clockActive = !clockActive;
        updateClockBtn();
      }
    }

    function tickClockLoop() {
      const now = new Date();
      document.getElementById("wallClock").innerText = now.toTimeString().split(" ")[0];

      const sec = now.getSeconds();
      const ms = now.getMilliseconds();
      const waitSec = 60.0 - sec - (ms / 1000.0);
      document.getElementById("countdownTimer").innerText = `${waitSec.toFixed(1)}s`;

      const evapPct = Math.min(100.0, Math.max(0.0, ((sec + ms/1000.0) / 60.0) * 100.0));
      document.getElementById("evapBar").style.width = evapPct + "%";
      document.getElementById("evapText").innerText = `${evapPct.toFixed(0)}% evaporated`;

      // Trigger automatic draw only in direct WebSocket mode (server has its own scheduler)
      if (!isServerBackend && clockActive && wsConnected && !isDrawingJob && sec === 0 && now.getMinutes() !== lastDrawnMinute) {
        lastDrawnMinute = now.getMinutes();
        runPlotCycle(now);
      }
    }

    function triggerDrawNow() {
      if (isDrawingJob) return;
      if (isServerBackend) {
        fetch("/api/clock/trigger", {method: "POST"})
          .then(r => r.json())
          .then(d => {
            if (d.success) logTerm("⚡ Clock draw triggered on server!");
            else logTerm(`Error: ${d.error}`);
          });
      } else {
        runPlotCycle(new Date());
      }
    }

    // -------------------------------------------------------------------------
    // TOOLPATH OPTIMIZER & G-CODE GENERATOR
    // -------------------------------------------------------------------------
    function optimizeStrokes(strokes, startPos = [-350, 150]) {
      if (!strokes || strokes.length <= 1) {
        return {
          strokes: strokes || [],
          rapidDist: 0,
          penLifts: strokes ? strokes.length : 0
        };
      }
      const remaining = strokes.filter(s => s && s.length >= 2).map(s => s.map(pt => [pt[0], pt[1]]));
      const optimized = [];
      let curX = startPos[0];
      let curY = startPos[1];
      let rapidDist = 0;
      let penLifts = 0;

      while (remaining.length > 0) {
        let bestIdx = 0;
        let bestDist = Infinity;
        let bestReverse = false;

        for (let i = 0; i < remaining.length; i++) {
          const strk = remaining[i];
          const p0 = strk[0];
          const pN = strk[strk.length - 1];
          const dStart = Math.hypot(p0[0] - curX, p0[1] - curY);
          const dEnd = Math.hypot(pN[0] - curX, pN[1] - curY);

          if (dStart < bestDist) {
            bestDist = dStart;
            bestIdx = i;
            bestReverse = false;
          }
          if (dEnd < bestDist) {
            bestDist = dEnd;
            bestIdx = i;
            bestReverse = true;
          }
        }

        let chosen = remaining.splice(bestIdx, 1)[0];
        if (bestReverse) chosen.reverse();

        // Continuous chain welding (< 0.15mm endpoints) eliminates pen lift
        if (optimized.length > 0 && bestDist < 0.15) {
          optimized[optimized.length - 1].push(...chosen.slice(1));
        } else {
          optimized.push(chosen);
          rapidDist += bestDist;
          penLifts++;
        }
        const lastPt = chosen[chosen.length - 1];
        curX = lastPt[0];
        curY = lastPt[1];
      }
      return { strokes: optimized, rapidDist, penLifts };
    }

    function updatePathMetrics(strokesList) {
      if (!strokesList || strokesList.length === 0) {
        const banner = document.getElementById("pathMetricsBanner");
        if (banner) banner.innerText = "⚡ Ready (No strokes)";
        return;
      }
      const cx = parseFloat(document.getElementById("inpCenterX").value) || -350;
      const cy = parseFloat(document.getElementById("inpCenterY").value) || 150;
      const zUp = parseFloat(document.getElementById("inpZUp").value) || -0.5;
      const zDown = parseFloat(document.getElementById("inpZDown").value) || -4.5;
      const feedXY = parseInt(document.getElementById("inpFeedXY").value) || 9000;
      const feedZ = 1000;
      const rapidXY = 12000;
      const doOpt = document.getElementById("chkOptimizePaths") ? document.getElementById("chkOptimizePaths").checked : true;

      const opt = doOpt ? optimizeStrokes(strokesList, [cx, cy]) : {
        strokes: strokesList,
        rapidDist: 0,
        penLifts: strokesList.length
      };

      let drawDist = 0;
      opt.strokes.forEach(s => {
        for (let i = 1; i < s.length; i++) {
          drawDist += Math.hypot(s[i][0] - s[i-1][0], s[i][1] - s[i-1][1]);
        }
      });

      const drawTime = (drawDist / feedXY) * 60.0;
      const rapidTime = (opt.rapidDist / rapidXY) * 60.0;
      const zTime = opt.penLifts * 2.0 * (Math.abs(zUp - zDown) / feedZ) * 60.0;
      const totalSec = drawTime + rapidTime + zTime + (opt.penLifts * 0.04);

      const banner = document.getElementById("pathMetricsBanner");
      if (banner) {
        banner.innerText = `⚡ ${opt.strokes.length} paths (${opt.penLifts} lifts) | 📐 Draw: ${drawDist.toFixed(0)}mm | 🚀 Rapid: ${opt.rapidDist.toFixed(0)}mm | ⏱️ ~${totalSec.toFixed(1)}s`;
      }
    }

    function generateGcodeFromStrokes(strokesList, jobTitle = "Spirit Job") {
      const zUp = parseFloat(document.getElementById("inpZUp").value) || -0.5;
      const zDown = parseFloat(document.getElementById("inpZDown").value) || -4.5;
      const feedXY = parseInt(document.getElementById("inpFeedXY").value) || 9000;
      const feedZ = 1000;
      const rapidXY = 12000;
      const cx = parseFloat(document.getElementById("inpCenterX").value) || -350;
      const cy = parseFloat(document.getElementById("inpCenterY").value) || 150;
      const doOpt = document.getElementById("chkOptimizePaths") ? document.getElementById("chkOptimizePaths").checked : true;

      const optResult = doOpt ? optimizeStrokes(strokesList, [cx, cy]) : {
        strokes: strokesList,
        rapidDist: 0,
        penLifts: strokesList.length
      };

      const finalStrokes = optResult.strokes;
      let totalDrawDist = 0;

      const lines = [
        "; ================================================================",
        `; Spirit Optimized G-Code: ${jobTitle}`,
        `; Date: ${new Date().toLocaleString()}`,
        `; Center: X=${cx.toFixed(1)}, Y=${cy.toFixed(1)} | Z_UP=${zUp.toFixed(2)}, Z_DOWN=${zDown.toFixed(2)}`,
        `; Feedrates: XY_DRAW=${feedXY} mm/min, Z=${feedZ} mm/min, RAPID=${rapidXY} mm/min`,
        "; ================================================================",
        "G21 ; Units: Millimeters",
        "G90 ; Absolute Coordinates",
        "G92.1 ; Reset Temporary Work Coordinate Offsets",
        `G0 Z${zUp.toFixed(2)} F${feedZ} ; Safe Pen Height`,
        ""
      ];

      finalStrokes.forEach(pts => {
        if (!pts || pts.length < 2) return;
        lines.push(`G0 X${pts[0][0].toFixed(3)} Y${pts[0][1].toFixed(3)} F${rapidXY}`);
        lines.push(`G1 Z${zDown.toFixed(2)} F${feedZ}`);
        for (let i = 1; i < pts.length; i++) {
          lines.push(`G1 X${pts[i][0].toFixed(3)} Y${pts[i][1].toFixed(3)} F${feedXY}`);
          totalDrawDist += Math.hypot(pts[i][0] - pts[i-1][0], pts[i][1] - pts[i-1][1]);
        }
        lines.push(`G0 Z${zUp.toFixed(2)} F${feedZ}`);
      });

      lines.push("");
      lines.push(`G0 X${cx.toFixed(3)} Y${cy.toFixed(3)} F${rapidXY} ; Park Center`);
      lines.push("G4 P0.05 ; Wait for motions to finish");

      updatePathMetrics(strokesList);
      return lines;
    }

    function downloadCurrentGcode(filename) {
      if (!currentStrokes || currentStrokes.length === 0) {
        alert("No strokes generated to download!");
        return;
      }
      const titleEl = document.getElementById("canvasTitle");
      const title = titleEl ? titleEl.innerText : "Spirit Job";
      const lines = generateGcodeFromStrokes(currentStrokes, title);
      const text = lines.join("\n");
      const blob = new Blob([text], {type: "text/plain;charset=utf-8"});
      const url = URL.createObjectURL(blob);
      const a = document.createElement("a");
      a.href = url;
      a.download = filename;
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
      URL.revokeObjectURL(url);
      logTerm(`💾 Downloaded ${filename} (${lines.length} lines, ready to plot)`);
    }

    function plotCurrentVectorStrokes() {
      if (!wsConnected && !isServerBackend) {
        alert("Please connect to FluidNC WebSocket or Python Server first!");
        return;
      }
      if (isDrawingJob || currentStrokes.length === 0) return;

      const lines = generateGcodeFromStrokes(currentStrokes);
      isDrawingJob = true;
      const titleEl = document.getElementById("canvasTitle");
      const jobTitle = titleEl ? titleEl.innerText : "Vector Art Plot";
      logTerm(`>>> Plotting ${jobTitle} (${lines.length} lines)...`);

      if (isServerBackend) {
        fetch("/api/plot", {
          method: "POST",
          headers: {"Content-Type": "application/json"},
          body: JSON.stringify({lines: lines, title: jobTitle})
        }).then(r => r.json()).then(d => {
          if (!d.success) {
            logTerm(`Plot error: ${d.error}`);
            isDrawingJob = false;
          }
        }).catch(err => {
          logTerm(`Plot network error: ${err}`);
          isDrawingJob = false;
        });
        return;
      }

      // WebSocket streaming fallback
      let idx = 0;
      function sendNext() {
        if (idx >= lines.length) {
          isDrawingJob = false;
          document.getElementById("jobProgressBar").style.width = "100%";
          document.getElementById("jobProgressText").innerText = "Completed";
          logTerm("Plot completed successfully!");
          return;
        }
        const l = lines[idx++];
        sendWs(l + "\n");
        const pct = ((idx / lines.length) * 100).toFixed(0);
        document.getElementById("jobProgressBar").style.width = pct + "%";
        document.getElementById("jobProgressText").innerText = `${pct}% (${l})`;
        setTimeout(sendNext, 20);
      }
      sendNext();
    }

    function runPlotCycle(dt) {
      generateClockPreview(dt);
      plotCurrentVectorStrokes();
    }

    // -------------------------------------------------------------------------
    // VECTOR PREVIEW GENERATORS
    // -------------------------------------------------------------------------
    function generateClockPreview(dt = new Date()) {
      const cx = parseFloat(document.getElementById("inpCenterX").value);
      const cy = parseFloat(document.getElementById("inpCenterY").value);
      const radius = parseFloat(document.getElementById("inpRadius").value);
      const mirrorX = document.getElementById("chkMirrorX").checked;
      function mx(x) { return mirrorX ? (2 * cx - x) : x; }

      currentStrokes = [];

      // CIRCLE STYLE
      if (clockStyle === "circle") {
        const ring = [];
        for (let i = 0; i <= 64; i++) {
          const th = (i * 360 / 64) * Math.PI / 180;
          ring.push([mx(cx + radius * Math.cos(th)), cy + radius * Math.sin(th)]);
        }
        currentStrokes.push(ring);

        // 12 Ticks
        for (let h = 0; h < 12; h++) {
          const ang = (90 - h * 30) * Math.PI / 180;
          const tlen = (h % 3 === 0) ? radius * 0.13 : radius * 0.07;
          currentStrokes.push([
            [mx(cx + (radius - 3) * Math.cos(ang)), cy + (radius - 3) * Math.sin(ang)],
            [mx(cx + (radius - 3 - tlen) * Math.cos(ang)), cy + (radius - 3 - tlen) * Math.sin(ang)]
          ]);
        }
      }
      // SIMPLE SQUARE STYLE (Clean, Minimal, Fast, Zero Clutter!)
      else if (clockStyle === "square") {
        const half = radius;
        // Clean single continuous box
        currentStrokes.push([
          [mx(cx - half), cy - half],
          [mx(cx + half), cy - half],
          [mx(cx + half), cy + half],
          [mx(cx - half), cy + half],
          [mx(cx - half), cy - half]
        ]);
      }

      // Hands (for Circle and Minimal only - Square is kept ultra clean with digital time!)
      if (clockStyle === "circle" || clockStyle === "minimal") {
        const hVal = (dt.getHours() % 12) + dt.getMinutes() / 60.0;
        const mVal = dt.getMinutes();
        const hAng = (90 - hVal * 30) * Math.PI / 180;
        const mAng = (90 - mVal * 6) * Math.PI / 180;
        const hx = cx + radius * 0.48 * Math.cos(hAng);
        const hy = cy + radius * 0.48 * Math.sin(hAng);
        const mxPt = cx + radius * 0.75 * Math.cos(mAng);
        const my = cy + radius * 0.75 * Math.sin(mAng);

        currentStrokes.push([[mx(cx), cy], [mx(hx), hy]]);
        currentStrokes.push([[mx(cx), cy], [mx(mxPt), my]]);
      }

      // Digital Hershey Text (for Square, Minimal, Digits - Circle is pure analog dial & hands!)
      if (clockStyle !== "circle") {
        const timeStr = `${String(dt.getHours()).padStart(2, '0')}:${String(dt.getMinutes()).padStart(2, '0')}`;
        const charH = (clockStyle === "digits" || clockStyle === "square") ? radius * 0.52 : radius * 0.22;
        const charW = charH * 0.58;
        const charSp = charW * 0.28;
        const totalW = timeStr.length * charW + (timeStr.length - 1) * charSp;
        let curX = cx - totalW / 2.0;
        const textY = (clockStyle === "digits" || clockStyle === "square") ? cy - charH / 2.0 : cy - radius * 0.52;

        for (let ch of timeStr) {
          if (HERSHEY[ch]) {
            for (let stroke of HERSHEY[ch]) {
              const pts = [];
              for (let pt of stroke) {
                pts.push([mx(curX + pt[0] * (charW / 16.0)), textY + pt[1] * (charH / 20.0)]);
              }
              currentStrokes.push(pts);
            }
          }
          curX += charW + charSp;
        }
      }
    }

    // -------------------------------------------------------------------------
    // MODE 2A: CANVAS TEXT & TYPOGRAPHY GENERATOR (Hershey Font, Drag/Click Anchor)
    // -------------------------------------------------------------------------
    function setArtSubMode(mode) {
      artSubMode = mode;
      const btnText = document.getElementById("btnSubText");
      const btnArt = document.getElementById("btnSubArt");
      const pnlText = document.getElementById("panelSubText");
      const pnlArt = document.getElementById("panelSubArt");
      if (mode === "text") {
        if (btnText) btnText.className = "btn btn-primary btn-sm";
        if (btnArt) btnArt.className = "btn btn-outline btn-sm";
        if (pnlText) pnlText.style.display = "block";
        if (pnlArt) pnlArt.style.display = "none";
        generateCanvasText();
      } else {
        if (btnText) btnText.className = "btn btn-outline btn-sm";
        if (btnArt) btnArt.className = "btn btn-primary btn-sm";
        if (pnlText) pnlText.style.display = "none";
        if (pnlArt) pnlArt.style.display = "block";
        generateVoiceArt();
      }
    }

    function setCanvasTextPrompt(txt) {
      const el = document.getElementById("inpCanvasText");
      if (el) el.value = txt;
      generateCanvasText();
    }

    function centerCanvasText() {
      const cx = parseFloat(document.getElementById("inpCenterX").value) || -350;
      const cy = parseFloat(document.getElementById("inpCenterY").value) || 150;
      const elX = document.getElementById("inpCanvasTextX");
      const elY = document.getElementById("inpCanvasTextY");
      if (elX) elX.value = cx.toFixed(1);
      if (elY) elY.value = cy.toFixed(1);
      generateCanvasText();
    }

    function generateCanvasText() {
      const textEl = document.getElementById("inpCanvasText");
      const rawText = textEl ? textEl.value : "";
      const sizeEl = document.getElementById("inpTextSize");
      const charH = sizeEl ? (parseFloat(sizeEl.value) || 28) : 28;
      const alignEl = document.getElementById("selTextAlign");
      const align = alignEl ? alignEl.value : "center";
      const xEl = document.getElementById("inpCanvasTextX");
      const yEl = document.getElementById("inpCanvasTextY");
      const anchorX = xEl ? (parseFloat(xEl.value) || -350) : -350;
      const anchorY = yEl ? (parseFloat(yEl.value) || 150) : 150;
      const cx = parseFloat(document.getElementById("inpCenterX").value) || -350;
      const mirrorX = document.getElementById("chkMirrorX").checked;
      function mx(x) { return mirrorX ? (2 * cx - x) : x; }

      currentStrokes = [];

      const lines = rawText.split("\n");
      const charW = charH * 0.60;
      const charSp = charW * 0.28;
      const spaceWidth = charW * 0.65;
      const lineHeight = charH * 1.45;

      const totalBlockHeight = (lines.length - 1) * lineHeight;
      const topCenterY = anchorY + (totalBlockHeight / 2.0);

      function calcLineWidth(str) {
        let w = 0;
        for (let i = 0; i < str.length; i++) {
          const ch = str[i];
          if (ch === ' ') {
            w += spaceWidth;
          } else {
            w += charW;
          }
          if (i < str.length - 1) w += charSp;
        }
        return w;
      }

      for (let li = 0; li < lines.length; li++) {
        const line = lines[li];
        const lineY = topCenterY - li * lineHeight;
        const lineBaseY = lineY - charH / 2.0;
        const lineWidth = calcLineWidth(line);

        let lineStartX;
        if (align === "left") {
          lineStartX = anchorX;
        } else if (align === "right") {
          lineStartX = anchorX - lineWidth;
        } else {
          lineStartX = anchorX - lineWidth / 2.0;
        }

        let curX = lineStartX;
        for (let i = 0; i < line.length; i++) {
          const ch = line[i];
          const glyph = HERSHEY[ch] || HERSHEY[ch.toUpperCase()];

          if (ch === ' ') {
            curX += spaceWidth + charSp;
            continue;
          }

          if (glyph) {
            for (let stroke of glyph) {
              const pts = [];
              for (let pt of stroke) {
                const px = curX + pt[0] * (charW / 16.0);
                const py = lineBaseY + pt[1] * (charH / 20.0);
                pts.push([mx(px), py]);
              }
              if (pts.length >= 2) {
                currentStrokes.push(pts);
              }
            }
          }
          curX += charW + charSp;
        }
      }

      const previewSnippet = rawText.trim().replace(/\n/g, ' ').substring(0, 32);
      const titleTag = previewSnippet ? `"${previewSnippet}${rawText.length > 32 ? '...' : ''}"` : "(Empty)";
      const titleEl = document.getElementById("canvasTitle");
      if (titleEl) {
        titleEl.innerText = `✍️ Type on Canvas: ${titleTag} [${currentStrokes.length} Strokes]`;
      }
    }

    // -------------------------------------------------------------------------
    // MODE 2B: VOICE & LINE ART PRESETS
    // -------------------------------------------------------------------------
    function toggleVoiceRecognition() {
      const SpeechRec = window.SpeechRecognition || window.webkitSpeechRecognition;
      if (!SpeechRec) {
        alert("Web Speech API not supported in this browser. Please type in the prompt box.");
        return;
      }
      const recog = new SpeechRec();
      recog.lang = 'en-US';
      const statusEl = document.getElementById("voiceStatus");
      const btnMic = document.getElementById("btnMic");

      statusEl.innerText = "🎙️ Listening... Speak now (e.g. 'origami bird', 'cat', 'flower')";
      btnMic.style.background = "#f43f5e";

      recog.onresult = function(e) {
        const text = e.results[0][0].transcript.toLowerCase();
        document.getElementById("inpVoicePrompt").value = text;
        statusEl.innerText = `Recognized: "${text}"`;
        btnMic.style.background = "";
        generateVoiceArt();
      };
      recog.onerror = function(e) {
        statusEl.innerText = `Mic error: ${e.error}. Try typing below.`;
        btnMic.style.background = "";
      };
      recog.onend = function() {
        btnMic.style.background = "";
      };
      recog.start();
    }

    function setAndGenPrompt(prompt) {
      document.getElementById("inpVoicePrompt").value = prompt;
      generateVoiceArt();
    }

    function generateVoiceArt() {
      const rawPrompt = document.getElementById("inpVoicePrompt").value.trim().toLowerCase();
      const cx = parseFloat(document.getElementById("inpCenterX").value);
      const cy = parseFloat(document.getElementById("inpCenterY").value);
      const scale = parseFloat(document.getElementById("inpRadius").value) * 1.8;
      const mirrorX = document.getElementById("chkMirrorX").checked;
      function mx(x) { return mirrorX ? (2 * cx - x) : x; }

      // Find matching preset
      let matchedKey = Object.keys(VECTOR_LIBRARY).find(k => rawPrompt.includes(k)) || 'origami bird';
      const rawVector = VECTOR_LIBRARY[matchedKey];
      currentStrokes = [];

      rawVector.forEach(path => {
        const transformedPath = path.map(pt => {
          // pt is [0..100]
          const xWorld = cx + (pt[0] - 50) * (scale / 100.0);
          const yWorld = cy + (pt[1] - 50) * (scale / 100.0);
          return [mx(xWorld), yWorld];
        });
        currentStrokes.push(transformedPath);
      });
      document.getElementById("canvasTitle").innerText = `Voice/Text Art: "${matchedKey.toUpperCase()}"`;
    }

    // -------------------------------------------------------------------------
    // MODE 3: MANDALA & SACRED GEOMETRY
    // -------------------------------------------------------------------------
    function randomizeMandala() {
      document.getElementById("inpMandalaSymmetry").value = [6, 8, 10, 12, 16][Math.floor(Math.random() * 5)];
      document.getElementById("inpMandalaFreq").value = Math.floor(Math.random() * 8) + 2;
      document.getElementById("inpMandalaLayers").value = Math.floor(Math.random() * 4) + 1;
      generateMandala();
    }

    function generateMandala() {
      const mType = document.getElementById("selMandalaType").value;
      const symm = parseInt(document.getElementById("inpMandalaSymmetry").value);
      const freq = parseInt(document.getElementById("inpMandalaFreq").value);
      const radius = parseFloat(document.getElementById("inpMandalaRadius").value);
      const layers = parseInt(document.getElementById("inpMandalaLayers").value);
      const cx = parseFloat(document.getElementById("inpCenterX").value);
      const cy = parseFloat(document.getElementById("inpCenterY").value);
      const mirrorX = document.getElementById("chkMirrorX").checked;
      function mx(x) { return mirrorX ? (2 * cx - x) : x; }

      currentStrokes = [];

      if (mType === "lotus") {
        for (let l = 1; l <= layers; l++) {
          const rLayer = radius * (l / layers);
          const pts = [];
          const steps = 180;
          for (let i = 0; i <= steps; i++) {
            const th = (i * 2 * Math.PI) / steps;
            const r = rLayer * (0.75 + 0.25 * Math.cos(symm * th));
            pts.push([mx(cx + r * Math.cos(th)), cy + r * Math.sin(th)]);
          }
          currentStrokes.push(pts);
        }
      } else if (mType === "spiro") {
        const R = radius * 0.7;
        const r = R * (freq / (freq + symm));
        const d = radius * 0.45;
        const steps = 400;
        const pts = [];
        for (let i = 0; i <= steps; i++) {
          const t = (i * 2 * Math.PI * freq) / steps;
          const x = (R - r) * Math.cos(t) + d * Math.cos((R - r) * t / r);
          const y = (R - r) * Math.sin(t) - d * Math.sin((R - r) * t / r);
          pts.push([mx(cx + x), cy + y]);
        }
        currentStrokes.push(pts);
      } else if (mType === "harmonograph") {
        const steps = 500;
        const pts = [];
        for (let i = 0; i <= steps; i++) {
          const t = (i * 12.0 * Math.PI) / steps;
          const decay = Math.exp(-0.003 * i);
          const x = radius * Math.sin(freq * t + 0.5) * decay;
          const y = radius * Math.sin((symm / 2) * t) * decay;
          pts.push([mx(cx + x), cy + y]);
        }
        currentStrokes.push(pts);
      } else if (mType === "flower") {
        // Center circle
        const ring0 = [];
        for (let i = 0; i <= 48; i++) {
          const th = (i * 2 * Math.PI) / 48;
          ring0.push([mx(cx + (radius/2) * Math.cos(th)), cy + (radius/2) * Math.sin(th)]);
        }
        currentStrokes.push(ring0);

        for (let p = 0; p < symm; p++) {
          const ang = (p * 2 * Math.PI) / symm;
          const ox = cx + (radius / 2) * Math.cos(ang);
          const oy = cy + (radius / 2) * Math.sin(ang);
          const ring = [];
          for (let i = 0; i <= 48; i++) {
            const th = (i * 2 * Math.PI) / 48;
            ring.push([mx(ox + (radius/2) * Math.cos(th)), oy + (radius/2) * Math.sin(th)]);
          }
          currentStrokes.push(ring);
        }
      }
      document.getElementById("canvasTitle").innerText = `Mandala: ${mType.toUpperCase()} (Symmetry ${symm})`;
    }

    // -------------------------------------------------------------------------
    // MODE 4: FLOW FIELDS & LINE ART
    // -------------------------------------------------------------------------
    function randomizeFlowArt() {
      document.getElementById("inpFlowSeed").value = Math.floor(Math.random() * 1000);
      generateFlowArt();
    }

    // Pseudo-random noise helper for flow field
    function pseudoNoise(x, y, seed) {
      const s = Math.sin(x * 12.9898 + y * 78.233 + seed * 43.123) * 43758.5453;
      return s - Math.floor(s);
    }

    function generateFlowArt() {
      const fType = document.getElementById("selFlowType").value;
      const linesCount = parseInt(document.getElementById("inpFlowLines").value);
      const stepsCount = parseInt(document.getElementById("inpFlowSteps").value);
      const curl = parseFloat(document.getElementById("inpFlowCurl").value);
      const seed = parseFloat(document.getElementById("inpFlowSeed").value);
      const cx = parseFloat(document.getElementById("inpCenterX").value);
      const cy = parseFloat(document.getElementById("inpCenterY").value);
      const radius = parseFloat(document.getElementById("inpRadius").value);
      const mirrorX = document.getElementById("chkMirrorX").checked;
      function mx(x) { return mirrorX ? (2 * cx - x) : x; }

      currentStrokes = [];

      if (fType === "perlin") {
        for (let l = 0; l < linesCount; l++) {
          let px = cx - radius * 0.9 + (l / linesCount) * (radius * 1.8);
          let py = cy - radius * 0.6 + (Math.sin(l) * 20);
          const stroke = [[mx(px), py]];

          for (let s = 0; s < stepsCount; s++) {
            const n = pseudoNoise(px / curl, py / curl, seed) * Math.PI * 2;
            px += Math.cos(n) * 3.5;
            py += Math.sin(n) * 3.5;
            if (Math.hypot(px - cx, py - cy) > radius * 1.1) break;
            stroke.push([mx(px), py]);
          }
          if (stroke.length > 3) currentStrokes.push(stroke);
        }
      } else if (fType === "vortex") {
        for (let l = 0; l < linesCount; l++) {
          const ang0 = (l * 2 * Math.PI) / linesCount;
          let r = radius * 0.95;
          const stroke = [];
          for (let s = 0; s < stepsCount; s++) {
            const a = ang0 + s * (curl / 100.0);
            r *= 0.97;
            stroke.push([mx(cx + r * Math.cos(a)), cy + r * Math.sin(a)]);
          }
          currentStrokes.push(stroke);
        }
      } else if (fType === "waves") {
        for (let l = 0; l < linesCount; l++) {
          const yRow = cy - radius * 0.8 + (l / linesCount) * (radius * 1.6);
          const stroke = [];
          for (let s = 0; s <= stepsCount; s++) {
            const xCol = cx - radius + (s / stepsCount) * (radius * 2);
            const waveY = yRow + Math.sin(s * 0.25 + l * 0.5 + seed) * (curl * 0.4);
            stroke.push([mx(xCol), waveY]);
          }
          currentStrokes.push(stroke);
        }
      } else if (fType === "crystals") {
        for (let l = 0; l < linesCount; l++) {
          const th = (l * 2 * Math.PI) / linesCount;
          const r1 = radius * (0.3 + 0.7 * pseudoNoise(l, 1, seed));
          const r2 = radius * (0.3 + 0.7 * pseudoNoise(l, 2, seed));
          const th2 = th + 0.4;
          currentStrokes.push([
            [mx(cx), cy],
            [mx(cx + r1 * Math.cos(th)), cy + r1 * Math.sin(th)],
            [mx(cx + r2 * Math.cos(th2)), cy + r2 * Math.sin(th2)],
            [mx(cx), cy]
          ]);
        }
      }
      document.getElementById("canvasTitle").innerText = `Line Art: ${fType.toUpperCase()} (${linesCount} Streams)`;
    }

    // -------------------------------------------------------------------------
    // CANVAS RENDERING & INTERACTION
    // -------------------------------------------------------------------------
    function mapX(x, width) {
      const pad = 25;
      return pad + ((x - (-730)) / 730.0) * (width - pad * 2);
    }

    function mapY(y, height) {
      const pad = 20;
      return height - pad - (y / 300.0) * (height - pad * 2);
    }

    function unmapX(px, width) {
      const pad = 25;
      const x = -730.0 + ((px - pad) / (width - pad * 2)) * 730.0;
      return Math.max(-730.0, Math.min(0.0, x));
    }

    function unmapY(py, height) {
      const pad = 20;
      const y = ((height - pad - py) / (height - pad * 2)) * 300.0;
      return Math.max(0.0, Math.min(300.0, y));
    }

    function getCanvasBedCoords(e) {
      const rect = canvas.getBoundingClientRect();
      const scaleX = canvas.width / rect.width;
      const scaleY = canvas.height / rect.height;
      const px = (e.clientX - rect.left) * scaleX;
      const py = (e.clientY - rect.top) * scaleY;
      return {
        x: unmapX(px, canvas.width),
        y: unmapY(py, canvas.height)
      };
    }

    function setupCanvasInteraction() {
      canvas.addEventListener("mousemove", function(e) {
        const pt = getCanvasBedCoords(e);
        const curEl = document.getElementById("canvasCursorPos");
        if (curEl) {
          curEl.style.display = "inline";
          curEl.innerText = `🖱️ X: ${pt.x.toFixed(1)} | Y: ${pt.y.toFixed(1)} mm`;
        }

        if (currentTab === "voice" && artSubMode === "text") {
          canvas.style.cursor = "crosshair";
          if (isDraggingText) {
            updateTextPositionFromBedCoord(pt.x, pt.y);
          }
        } else {
          canvas.style.cursor = "default";
        }
      });

      canvas.addEventListener("mouseleave", function() {
        const curEl = document.getElementById("canvasCursorPos");
        if (curEl) curEl.style.display = "none";
        isDraggingText = false;
      });

      canvas.addEventListener("mousedown", function(e) {
        if (e.button !== 0) return;
        if (currentTab === "voice" && artSubMode === "text") {
          isDraggingText = true;
          const pt = getCanvasBedCoords(e);
          updateTextPositionFromBedCoord(pt.x, pt.y);
        }
      });

      window.addEventListener("mouseup", function() {
        isDraggingText = false;
      });
    }

    function updateTextPositionFromBedCoord(bedX, bedY) {
      const cx = parseFloat(document.getElementById("inpCenterX").value) || -350;
      const mirrorX = document.getElementById("chkMirrorX").checked;
      const modelX = mirrorX ? (2 * cx - bedX) : bedX;
      const modelY = bedY;

      const elX = document.getElementById("inpCanvasTextX");
      const elY = document.getElementById("inpCanvasTextY");
      if (elX) elX.value = modelX.toFixed(1);
      if (elY) elY.value = modelY.toFixed(1);
      generateCanvasText();
    }

    function renderCanvas() {
      const w = canvas.width;
      const h = canvas.height;
      ctx.clearRect(0, 0, w, h);

      // Bed Grid
      ctx.strokeStyle = "#16202c";
      ctx.lineWidth = 1;
      for (let x = -700; x <= 0; x += 100) {
        const sx = mapX(x, w);
        ctx.beginPath(); ctx.moveTo(sx, mapY(0, h)); ctx.lineTo(sx, mapY(300, h)); ctx.stroke();
      }
      for (let y = 0; y <= 300; y += 50) {
        const sy = mapY(y, h);
        ctx.beginPath(); ctx.moveTo(mapX(-730, w), sy); ctx.lineTo(mapX(0, w), sy); ctx.stroke();
      }

      // Drawing Strokes (Glowing Water Blue)
      ctx.shadowBlur = 10;
      ctx.shadowColor = "rgba(56, 189, 248, 0.45)";
      ctx.strokeStyle = "#38bdf8";
      ctx.lineWidth = 2.4;

      currentStrokes.forEach(pts => {
        if (!pts || pts.length < 2) return;
        ctx.beginPath();
        ctx.moveTo(mapX(pts[0][0], w), mapY(pts[0][1], h));
        for (let i = 1; i < pts.length; i++) {
          ctx.lineTo(mapX(pts[i][0], w), mapY(pts[i][1], h));
        }
        ctx.stroke();
      });

      // Text Anchor Visualizer (When in Type on Canvas sub-mode)
      if (currentTab === "voice" && artSubMode === "text") {
        const xEl = document.getElementById("inpCanvasTextX");
        const yEl = document.getElementById("inpCanvasTextY");
        const anchorX = xEl ? (parseFloat(xEl.value) || -350) : -350;
        const anchorY = yEl ? (parseFloat(yEl.value) || 150) : 150;
        const cx = parseFloat(document.getElementById("inpCenterX").value) || -350;
        const mirrorX = document.getElementById("chkMirrorX").checked;
        const visAnchorX = mirrorX ? (2 * cx - anchorX) : anchorX;
        const ax = mapX(visAnchorX, w);
        const ay = mapY(anchorY, h);

        ctx.save();
        ctx.shadowBlur = 10;
        ctx.shadowColor = "#38bdf8";
        ctx.strokeStyle = "rgba(56, 189, 248, 0.9)";
        ctx.fillStyle = "#38bdf8";
        ctx.lineWidth = 1.5;

        // Anchor center dot
        ctx.beginPath();
        ctx.arc(ax, ay, 4, 0, Math.PI * 2);
        ctx.fill();

        // Crosshair reticle
        ctx.beginPath();
        ctx.moveTo(ax - 12, ay); ctx.lineTo(ax + 12, ay);
        ctx.moveTo(ax, ay - 12); ctx.lineTo(ax, ay + 12);
        ctx.stroke();

        // Concentric target ring
        ctx.strokeStyle = "rgba(56, 189, 248, 0.4)";
        ctx.beginPath();
        ctx.arc(ax, ay, 10, 0, Math.PI * 2);
        ctx.stroke();

        // Text coordinates tag
        ctx.font = "11px monospace";
        ctx.fillStyle = "rgba(201, 209, 217, 0.85)";
        ctx.fillText(`Anchor: (${anchorX.toFixed(0)}, ${anchorY.toFixed(0)})`, ax + 14, ay - 6);
        ctx.restore();
      }

      // Toolhead Crosshair (Red indicator)
      const tx = mapX(toolPos[0], w);
      const ty = mapY(toolPos[1], h);
      ctx.shadowBlur = 15;
      ctx.shadowColor = "#f43f5e";
      ctx.fillStyle = "#f43f5e";
      ctx.beginPath(); ctx.arc(tx, ty, 6, 0, Math.PI * 2); ctx.fill();

      ctx.strokeStyle = "rgba(244, 63, 94, 0.6)";
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      ctx.moveTo(tx - 12, ty); ctx.lineTo(tx + 12, ty);
      ctx.moveTo(tx, ty - 12); ctx.lineTo(tx, ty + 12);
      ctx.stroke();

      requestAnimationFrame(renderCanvas);
    }
  </script>
</body>
</html>
"""

@app.route("/")
@app.route("/clock.html")
def index():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    clock_file = os.path.join(script_dir, "clock.html")
    if os.path.exists(clock_file):
        bottle.response.set_header("Cache-Control", "no-cache, no-store, must-revalidate")
        return bottle.static_file("clock.html", root=script_dir)
    return HTML_PAGE

@app.route("/clock.html.gz")
def serve_gz():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    bottle.response.set_header("Content-Encoding", "gzip")
    bottle.response.set_header("Content-Type", "text/html; charset=utf-8")
    return bottle.static_file("clock.html.gz", root=script_dir)


# ---------------------------------------------------------------------------
# Server Launcher
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="Spirit Live Water Clock Web Dashboard")
    parser.add_argument("--host", type=str, default="0.0.0.0", help="Web server host IP")
    parser.add_argument("--port", type=int, default=8080, help="Web server port (default 8080)")
    parser.add_argument("--serial", type=str, default="auto", help="Auto-connect serial port")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate")
    args = parser.parse_args()

    print("=" * 65)
    print("      Spirit Live Water Clock Controller & Dashboard")
    print("=" * 65)
    print(f" Web UI available at: http://localhost:{args.port}")
    print(f" Network access at:   http://<Device_IP>:{args.port}")
    print("=" * 65)

    # Attempt initial auto-connection
    if args.serial:
        t = threading.Thread(target=lambda: manager.connect(args.serial, args.baud), daemon=True)
        t.start()

    bottle.run(app, host=args.host, port=args.port, quiet=True)


if __name__ == "__main__":
    main()
