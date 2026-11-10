# PolyPlot Raspberry Pi USB Interface & Generator Development Guide

This guide is written for full-stack developers building a custom toolpath generator, web dashboard, or control backend running on a **Raspberry Pi** interfaced with the **PolyPlot Water Plotter** via USB.

---

## 1. System Architecture Overview

```
+-------------------------------------------------------------------------+
|                              Raspberry Pi                               |
|                                                                         |
|  +---------------------------+       +-------------------------------+  |
|  | Web Frontend / Mobile App | <---> | Backend (Node.js / Python)    |  |
|  | (React / Vue / Svelte)    |  WS   | - Path Generator & Optimizer  |  |
|  |                           | REST  | - G-Code Streamer & Serial Mgr|  |
|  +---------------------------+       +---------------+---------------+  |
+------------------------------------------------------|------------------+
                                                       |
                                            USB Cable (Virtual COM)
                                            115200 Baud, 8-N-1
                                                       |
+------------------------------------------------------v------------------+
| Makerbase MKS DLC32 V2.1 Controller (ESP32-WROOM-32U running FluidNC)   |
|                                                                         |
|  - Axis X: Steps=200/mm | MaxRate=12000 mm/min | Travel = [-730, 0] mm  |
|  - Axis Y: Steps=200/mm | MaxRate=12000 mm/min | Travel = [0, 300] mm   |
|  - Axis Z: Steps=200/mm | MaxRate=1000 mm/min  | Travel = [-10, 0] mm   |
|  - Kinematics: CoreXY | Actuator: Water Pen / Slate                     |
+-------------------------------------------------------------------------+
```

---

## 2. Raspberry Pi Hardware & OS Setup

### 2.1 Physical Connection & Power
- **USB Port:** Connect any USB-A port on the Raspberry Pi to the USB-C / USB-B port of the MKS DLC32.
- **Power Supply Separation (Important):**
  - The MKS DLC32 must be powered by its own **12V / 24V DC power supply** (to drive the CoreXY stepper motors).
  - The Raspberry Pi must be powered by its official USB-C power supply.
  - *Do NOT attempt to power the Raspberry Pi from the DLC32, or vice versa.*

### 2.2 Serial Device Identification
On Raspberry Pi OS (Debian Linux), the CH340 / CP2102 USB-to-UART chip enumerates under `/dev/`:
```bash
ls -l /dev/ttyUSB*
# Or via unique device ID (recommended):
ls -l /dev/serial/by-id/
```

### 2.3 User Permissions
Give your backend user permission to access serial ports without `sudo`:
```bash
sudo usermod -a -G dialout $USER
```
*(Log out and log back in or reboot for changes to take effect).*

### 2.4 Persistent Device Symlink (Udev Rule)
To prevent the port from randomly alternating between `/dev/ttyUSB0` and `/dev/ttyUSB1` when re-plugged, create a persistent rule:
```bash
sudo nano /etc/udev/rules.d/99-polyplot.rules
```
Add the following line (matches the CH340 vendor `1a86` and product `7523`):
```udev
SUBSYSTEM=="tty", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", MODE="0666", SYMLINK+="polyplot"
```
Reload udev rules:
```bash
sudo udevadm control --reload-rules && sudo udevadm trigger
```
Now your backend can always connect to `/dev/polyplot` with 100% reliability!

---

## 3. Machine Physical Limits & Kinematics

### 3.1 Coordinate System & Limits
The machine uses **CoreXY kinematics**. Homing limits define the operational boundary:

| Axis | Homing Position | Direction | Valid Range | Physical Center |
|---|---|---|---|---|
| **X** | Right Endstop (`X = 0.0`) | Homing Max | **`-730.0 mm` to `0.0 mm`** | **`X = -350.0 mm`** |
| **Y** | Front Endstop (`Y = 0.0`) | Homing Min | **`0.0 mm` to `300.0 mm`** | **`Y = 150.0 mm`** |
| **Z** | Top Limit (`Z = 0.0`) | Homing Max | **`-10.0 mm` to `0.0 mm`** | N/A |

> [!WARNING]
> Notice that **X is strictly negative**! Coordinates with $X > 0.0$ or $X < -730.0$ will trigger soft limit alarms or machine crashes.

### 3.2 Calibrated Pen Depths
- **Pen UP (Safe Rapid Travel):** `Z = -0.50 mm`
- **Pen DOWN (Draw Contact):** `Z = -4.50 mm`
*(Values between $-3.0$ and $-4.0\text{ mm}$ do not make solid contact with the slate; $-4.50\text{ mm}$ is calibrated).*

### 3.3 Safe Feedrates (Speeds)
- **Rapid Travel (XY):** `F12000` (12,000 mm/min = 200 mm/s)
- **Drawing Feedrate (XY):** `F6000` to `F9000` (9,000 mm/min = 150 mm/s recommended)
- **Pen Lift / Plunge (Z):** `F1000` (1,000 mm/min)
  - **CRITICAL:** FluidNC firmware hard-caps the Z axis at $1000\text{ mm/min}$. Never send `F2000` on Z moves, as it will be rejected or cause velocity step mismatch.

---

## 4. Glass Mirroring & Right-to-Left Physical Writing

The machine draws on the water slate/glass which is viewed by spectators from the front. If you plot standard text or asymmetrical designs, they will appear **backwards (mirror-reversed)** to the viewer.

### 4.1 Transformation Formula:
Apply horizontal inversion across Center X (`-350.0 mm`):
$$X_{\text{mirrored}} = 2 \cdot C_X - X = 2 \cdot (-350.0) - X = -700.0 - X$$

#### Python Helper:
```python
def mirror_x(x: float, center_x: float = -350.0) -> float:
    return 2.0 * center_x - x
```

#### JavaScript / TypeScript Helper:
```typescript
function mirrorX(x: number, centerX: number = -350.0): number {
  return 2.0 * centerX - x;
}
```

### 4.2 Right-to-Left Machine Motion $\iff$ Left-to-Right Visual Viewing
In the machine's physical frame:
- $X = 0.0\text{ mm}$ is the **Right endstop**.
- $X = -730.0\text{ mm}$ is the **Left endstop**.

When a spectator stands on the **glass side (front of machine)**:
- Spectator's **LEFT** corresponds to Machine $X \approx 0.0$ (Machine Right).
- Spectator's **RIGHT** corresponds to Machine $X \approx -730.0$ (Machine Left).

**How it works during plotting:**
1. Text is modeled in standard left-to-right order (Word starts at $X = -400.0$, ends at $X = -300.0$).
2. Mirrored mapping translates the first letter to $X_{\text{mirrored}} = -700 - (-400) = \mathbf{-300.0\text{ mm}}$ (Machine Right).
3. The last letter maps to $X_{\text{mirrored}} = -700 - (-300) = \mathbf{-400.0\text{ mm}}$ (Machine Left).
4. **Physical machine motion:** The pen starts at $-300.0$ and moves towards $-400.0$ $\implies$ **Physically moving from RIGHT to LEFT**!
5. **Spectator view:** Because the spectator is looking from the front glass, they see the pen start on their **LEFT** and flow toward their **RIGHT**!

> [!IMPORTANT]
> When optimizing toolpaths for text, **always preserve forward reading order (`preserve_order = True`)**. Never allow a TSP optimizer to reverse a cursive word stroke, or the letters will be drawn backward.

---

## 4.3 Continuous Cursive Typography (Zero Pen Lifts)

Standard vector fonts (like Hershey block) draw letters with 3–5 disconnected strokes per character. A 5-letter word results in 15+ pen lifts, wasting 8+ seconds in Z-axis transit and leaving water droplets at every plunge point.

### The Continuous Cursive Solution:
In continuous running script:
- Every lowercase glyph enters at baseline $(X = 0, Y = 2)$ and exits at baseline $(X = W, Y = 2)$.
- The exit point of character $N$ matches the entry point of character $N+1$.
- **Result:** An entire word is **ONE single unbroken polyline**. Inside each word, there are **0 pen lifts**!
- **100% Unbroken Line Mode:** If desired, spaces between words can be connected with a subtle baseline flourish (`connect_spaces = True`), drawing an entire sentence in **literally 1 pen lift**!

```
        e.g. Word "water":
        Pen Down (Z -4.5) ──> 'w' ──> 'a' ──> 't' ──> 'e' ──> 'r' ──> Pen Up (Z -0.5)
        [Total Pen Lifts = 1 | Draw Time = ~2.4 seconds]
```

---

## 4.4 Pure Single-Line Art (One-Line Contours)

For graphics and illustrations, avoid segmented line art. Use **Single-Line Art** (continuous Eulerian paths):
- The entire illustration is drawn in **ONE continuous stroke**.
- **Pen Lifts = 1** (down at start, up at end).
- **Rapid Travel = 0 mm**.
- **Draw Time = 10 to 18 seconds**, ensuring the whole drawing stays glistening wet before evaporation begins!

**Included Presets in Generator:**
- `face`: Iconic Picasso-style single-line continuous facial profile.
- `cat`: Curled sleeping cat drawn in one fluid loop.
- `hummingbird`: Slender beak, swept-back wings, tail feathers.
- `rose`: Botanical stem, leaf loop, and spiraling blossom petals.
- `monstera`: Central stem vein with looping fenestrated leaf lobes.
- `butterfly`: Symmetrical antennae, upper and lower wing curves.
- `mountains`: Mountain peaks, rising sun loops, and foreground river.
- `hilbert`: Continuous 2D space-filling fractal curve.
- `spiral`: Dual inward-outward Archimedean spiral.

---

## 5. Serial Protocol & Streaming Handshake

### 5.1 Serial Connection Parameters
- **Baud Rate:** `115200`
- **Data Bits:** `8`
- **Parity:** `None`
- **Stop Bits:** `1`
- **Flow Control:** None (RTS/CTS and DTR off)
- **Line Terminator:** `\r\n` or `\n`

### 5.2 Line-by-Line Ping-Pong Handshake
FluidNC uses the standard Grbl protocol:
1. Send one G-code line ending with `\n` (e.g. `G1 X-300.000 Y150.000 F9000\n`).
2. Read incoming serial responses line-by-line until you receive either:
   - `ok\r\n` $\implies$ Command accepted and placed into the planner buffer.
   - `error:<id>\r\n` $\implies$ Syntax or parameter error (e.g., `error:20`).
3. Only send the next line after `ok` is received.

### 5.3 Real-Time Control Bytes (No Newline Needed)
These single-byte characters can be sent at **any time**, even while the machine is actively moving:

| Byte | Char | Action |
|---|---|---|
| `0x3F` | `?` | **Status Query:** FluidNC immediately replies with `<Idle|MPos:-350.0,150.0,-0.5|...>` |
| `0x21` | `!` | **Feed Hold:** Decelerates and halts motion smoothly. State changes to `Hold:0`. |
| `0x7E` | `~` | **Cycle Start / Resume:** Resumes motion from Feed Hold. |
| `0x18` | `Ctrl+X` | **Soft Reset:** Aborts motion immediately and clears internal buffer. |

### 5.4 CRITICAL Syntax Trap: `M400` vs `G4 P0.05`
> [!CAUTION]
> **DO NOT USE `M400`!**
> Marlin 3D printer firmware uses `M400` for motion synchronization. FluidNC / Grbl does **NOT** support `M400` and will throw `error:20: Unsupported or invalid g-code command`.
>
> **Always use dwell buffer drain:**
> ```gcode
> G4 P0.05 ; Drains the planner buffer and ensures all motions complete
> ```

---

## 6. Standard G-Code File Structure

Every generated file must adhere to this structure:

```gcode
; ================================================================
; PolyPlot G-Code Header
; Units: Millimeters | Coordinate: Absolute
; ================================================================
G21 ; Set units to millimeters
G90 ; Set positioning to absolute coordinates
G92.1 ; Reset temporary coordinate offsets (WCO)
G0 Z-0.50 F1000 ; Ensure pen is at safe transit height

; --- Stroke 1 ---
G0 X-380.000 Y120.000 F12000 ; Rapid travel to stroke start
G1 Z-4.50 F1000 ; Plunge pen to drawing slate
G1 X-380.000 Y180.000 F9000 ; Draw line
G1 X-320.000 Y180.000 F9000 ; Draw line
G0 Z-0.50 F1000 ; Lift pen to safe height

; --- Stroke 2 ---
G0 X-320.000 Y120.000 F12000 ; Rapid travel to next stroke start
G1 Z-4.50 F1000 ; Plunge pen
G1 X-380.000 Y120.000 F9000 ; Draw line
G0 Z-0.50 F1000 ; Lift pen

; --- Program End & Center Park ---
G0 Z-0.50 F1000 ; Guarantee pen lifted
G0 X-350.000 Y150.000 F12000 ; Park at bed center
G4 P0.05 ; Wait for all queued motions to physically finish
; ================================================================
```

---

## 7. Toolpath Optimization (Evaporation Management)

Water lines on slate evaporate in **30 to 90 seconds** depending on ambient room temperature and humidity. An unoptimized path that draws randomly across the bed will dry out before the piece is finished.

### Recommended Generator Algorithm:
1. **Continuous Segment Welding:**
   Merge consecutive polyline segments whose start and end points are within $0.15\text{ mm}$ into a single continuous `G1` stroke.
2. **Greedy Nearest-Neighbor Ordering (TSP):**
   Start from the current pen position. Pick the next unvisited stroke whose start (or end) is closest in Euclidean distance.
3. **Bidirectional Flipping:**
   If a stroke's endpoint is closer to the current pen than its start point, **reverse the stroke's vertex array** and draw it in reverse.
4. **Impact:**
   Reduces pen lifts by 40%–60% and slashes non-drawing rapid travel by 50%–75%.

---

## 8. Production Code Templates

### 8.1 Node.js / TypeScript Streaming Backend
Install dependencies:
```bash
npm install serialport @serialport/parser-readline
```

```typescript
import { SerialPort } from "serialport";
import { ReadlineParser } from "@serialport/parser-readline";

export class PolyPlotClient {
  private port: SerialPort;
  private parser: ReadlineParser;
  private isStreaming: boolean = false;

  constructor(path: string = "/dev/polyplot", baudRate: number = 115200) {
    this.port = new SerialPort({ path, baudRate, autoOpen: false });
    this.parser = this.port.pipe(new ReadlineParser({ delimiter: "\r\n" }));

    this.parser.on("data", (line: string) => {
      // Handle unsolicited messages or real-time status responses
      if (line.startsWith("<")) {
        this.handleStatusUpdate(line);
      }
    });
  }

  public async connect(): Promise<void> {
    return new Promise((resolve, reject) => {
      this.port.open((err) => (err ? reject(err) : resolve()));
    });
  }

  // Sends a single line and awaits 'ok' or 'error'
  public sendLine(gcode: string): Promise<string> {
    return new Promise((resolve, reject) => {
      const clean = gcode.trim();
      if (!clean || clean.startsWith(";")) return resolve("ok");

      const onData = (data: string) => {
        if (data.startsWith("ok")) {
          cleanup();
          resolve(data);
        } else if (data.startsWith("error:")) {
          cleanup();
          reject(new Error(`FluidNC Error: ${data} on line: ${clean}`));
        }
      };

      const cleanup = () => {
        this.parser.removeListener("data", onData);
      };

      this.parser.on("data", onData);
      this.port.write(clean + "\n", (err) => {
        if (err) {
          cleanup();
          reject(err);
        }
      });
    });
  }

  // Stream full G-code job line-by-line
  public async streamGcode(lines: string[], onProgress?: (pct: number) => void): Promise<void> {
    this.isStreaming = true;
    try {
      for (let i = 0; i < lines.length; i++) {
        await this.sendLine(lines[i]);
        if (onProgress) {
          onProgress(Math.round(((i + 1) / lines.length) * 100));
        }
      }
    } finally {
      this.isStreaming = false;
    }
  }

  // Real-time immediate commands
  public pollStatus(): void {
    if (!this.isStreaming) {
      this.port.write("?"); // Single byte query
    }
  }

  public pause(): void {
    this.port.write("!"); // Feed hold
  }

  public resume(): void {
    this.port.write("~"); // Cycle start
  }

  public emergencyAbort(): void {
    this.port.write("\x18"); // Ctrl+X soft reset
  }

  private handleStatusUpdate(rawStatus: string): void {
    // Example: <Idle|MPos:-350.000,150.000,-0.500|FS:0,0|WCO:0.000,0.000,0.000>
    const match = rawStatus.match(/<([A-Za-z]+)\|MPos:([-\d\.]+),([-\d\.]+),([-\d\.]+)/);
    if (match) {
      const [, state, x, y, z] = match;
      // Emit state to WebSocket or frontend
    }
  }
}
```

---

### 8.2 Python Backend Template
Install dependencies:
```bash
pip install pyserial
```

```python
import time
import serial
import threading
from typing import List, Callable, Optional

class PolyPlotController:
    def __init__(self, port: str = "/dev/polyplot", baudrate: int = 115200):
        self.port = port
        self.baudrate = baudrate
        self.ser: Optional[serial.Serial] = None
        self.lock = threading.Lock()
        self.is_streaming = False

    def connect(self):
        self.ser = serial.Serial(self.port, self.baudrate, timeout=1.0)
        time.sleep(1.5)  # Allow ESP32 reboot settle
        self.ser.reset_input_buffer()

    def send_line(self, line: str) -> str:
        clean = line.strip()
        if not clean or clean.startswith(";"):
            return "ok"

        with self.lock:
            self.ser.write((clean + "\n").encode("utf-8"))
            while True:
                resp = self.ser.readline().decode("utf-8", errors="ignore").strip()
                if resp.startswith("ok"):
                    return resp
                elif resp.startswith("error:"):
                    raise RuntimeError(f"FluidNC {resp} on line: '{clean}'")

    def stream_job(self, lines: List[str], progress_callback: Optional[Callable[[float], None]] = None):
        self.is_streaming = True
        try:
            total = len(lines)
            for idx, line in enumerate(lines):
                self.send_line(line)
                if progress_callback:
                    progress_callback(((idx + 1) / total) * 100.0)
        finally:
            self.is_streaming = False

    def query_status(self) -> str:
        if self.is_streaming:
            return ""
        with self.lock:
            self.ser.write(b"?")
            return self.ser.readline().decode("utf-8", errors="ignore").strip()

    def pause(self):
        with self.lock:
            self.ser.write(b"!")

    def resume(self):
        with self.lock:
            self.ser.write(b"~")

    def abort(self):
        with self.lock:
            self.ser.write(b"\x18")
```

---

## 9. Recommended REST & WebSocket API Design

If your backend serves a React, Vue, or mobile UI, implement these endpoints:

| Endpoint | Method | Payload / Action | Description |
|---|---|---|---|
| `/api/status` | `GET` | N/A | Returns `{ state, x, y, z, is_streaming }` |
| `/api/home` | `POST` | N/A | Sends `$H` to home axes safely |
| `/api/plot` | `POST` | `{ gcode: "..." }` | Streams an array of G-code lines |
| `/api/pause` | `POST` | N/A | Sends `!` feed hold |
| `/api/resume` | `POST` | N/A | Sends `~` cycle start |
| `/api/abort` | `POST` | N/A | Sends `\x18` soft reset |
| `/ws` | `WebSocket` | Live telemetry | Push updates every 250 ms (`MPos`, `state`, progress %) |

---

## 10. Common Pitfalls & Troubleshooting

1. **`error:20: Unsupported or invalid g-code command found in block`**
   - **Cause:** You sent `M400` or an unsupported M-code.
   - **Fix:** Replace with `G4 P0.05`.
2. **`error:9: G-code locked out during alarm or jog state`**
   - **Cause:** Controller booted up or triggered an endstop and is in `Alarm` state.
   - **Fix:** Send `$X` to unlock the alarm, or run homing with `$H`.
3. **Stuttering / 3-Second Delay Per Command**
   - **Cause:** A background thread is polling status (`?` or `read_all()`) and consuming the `ok\r\n` byte stream before the G-code streamer can see it.
   - **Fix:** Suppress background polling during active streaming.
4. **Pen Air-Drawing (Not Touching Water Slate)**
   - **Cause:** Drawing depth set to $-3.0\text{ mm}$ or $-4.0\text{ mm}$.
   - **Fix:** Calibrated drawing height is **`Z = -4.50 mm`**. Safe height is **`Z = -0.50 mm`**.
5. **Drawings are Backwards**
   - **Cause:** Did not apply horizontal mirroring across Center X.
   - **Fix:** Invert all X points with $X_{\text{mirrored}} = -700.0 - X$.
6. **Machine Faults on Z Motion**
   - **Cause:** Setting `F2000` or higher on Z moves.
   - **Fix:** Keep Z feedrate at or below **`F1000`**.

