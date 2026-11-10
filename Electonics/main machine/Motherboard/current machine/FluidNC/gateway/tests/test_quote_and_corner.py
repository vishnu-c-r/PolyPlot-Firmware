#!/usr/bin/env python3
import datetime
import os
import sys
import unittest

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

from clock_ui_server import (
    SAFE_CORNERS,
    get_closest_corner,
    DEFAULT_QUOTES,
    generate_clock_gcode_and_paths
)


class TestQuoteAndClosestCorner(unittest.TestCase):
    def test_closest_corner_selection(self):
        # Top-Left test
        c = get_closest_corner(-650.0, 270.0)
        self.assertEqual(c, (-680.0, 280.0))

        # Top-Right test
        c = get_closest_corner(-50.0, 260.0)
        self.assertEqual(c, (-20.0, 280.0))

        # Bottom-Left test
        c = get_closest_corner(-600.0, 40.0)
        self.assertEqual(c, (-680.0, 20.0))

        # Bottom-Right test
        c = get_closest_corner(-80.0, 30.0)
        self.assertEqual(c, (-20.0, 20.0))

    def test_quote_gcode_generation(self):
        dt = datetime.datetime(2026, 11, 9, 14, 30, 0)
        config = {
            "style": "quote",
            "center_x": -350.0,
            "center_y": 150.0,
            "radius": 110.0,
            "z_up": -0.5,
            "z_down": -4.5,
            "feed_xy": 12000,
            "feed_z": 1000,
            "rapid_xy": 12000,
            "reset_wco": True,
            "mirror_x": True,
            "quote_text": "TIME FLOWS LIKE WATER",
            "quote_rotate": False
        }
        lines, vector_strokes, dist, est = generate_clock_gcode_and_paths(dt, config)

        # 1. Output must not be empty
        self.assertGreater(len(lines), 20)
        self.assertGreater(len(vector_strokes), 5)
        self.assertGreater(dist, 100.0)
        self.assertLess(est, 45.0)  # Must finish within the 1-minute cycle!

        # 2. Both digits and quote strokes should be present
        types = set(s["type"] for s in vector_strokes)
        self.assertIn("digit", types)
        self.assertIn("quote", types)

        # 3. Last movement must park at one of the 4 safe corners
        last_move_line = [l for l in lines if l.startswith("G0 X") and "Park" in l][-1]
        self.assertIn("Park Closest Corner", last_move_line)

        # Check bounds: All X in [-700, 0], all Y in [0, 300], all Z >= -5.0
        for l in lines:
            if l.startswith(";"):
                continue
            if "X" in l and "Y" in l:
                parts = l.split()
                for p in parts:
                    if p.startswith("X"):
                        x_val = float(p[1:])
                        self.assertTrue(-700.0 <= x_val <= 0.0, f"X out of bounds: {x_val}")
                    elif p.startswith("Y"):
                        y_val = float(p[1:])
                        self.assertTrue(0.0 <= y_val <= 300.0, f"Y out of bounds: {y_val}")
            if "Z" in l:
                for p in l.split():
                    if p.startswith("Z"):
                        z_val = float(p[1:])
                        self.assertGreaterEqual(z_val, -5.0)

    def test_quote_auto_rotation(self):
        # Even minute
        dt0 = datetime.datetime(2026, 11, 9, 14, 0, 0)
        # Next minute
        dt1 = datetime.datetime(2026, 11, 9, 14, 1, 0)

        cfg = {"style": "quote", "quote_rotate": True}
        lines0, _, _, _ = generate_clock_gcode_and_paths(dt0, cfg)
        lines1, _, _, _ = generate_clock_gcode_and_paths(dt1, cfg)

        quote0 = [l for l in lines0 if l.startswith("; --- Quote")][0]
        quote1 = [l for l in lines1 if l.startswith("; --- Quote")][0]
        self.assertNotEqual(quote0, quote1)


if __name__ == "__main__":
    unittest.main()
