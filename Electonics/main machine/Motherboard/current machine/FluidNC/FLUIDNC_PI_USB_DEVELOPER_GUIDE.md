# FluidNC Machine Control via Spirit Gateway
## Fullstack Developer Integration & API Specification

---

### 1. System Architecture & Topology

```
┌────────────────────────────────────────────────────────┐
│                   Next.js Frontend                     │
│  - Drag & Drop .gcode upload                           │
│  - Real-time progress HUD (Line, %, MPos, Buffer)      │
│  - Jog controls & Closest Safe Corner Parking          │
│  - Water Clock & Motivational Quote Generator          │
└───────────────────────────┬────────────────────────────┘
                            │
               HTTP REST    │    WebSocket Events
             (Port 8080)    │    (Port 8081)
                            ▼
┌────────────────────────────────────────────────────────┐
│             Spirit Gateway Server (Raspberry Pi)       │
│  - Threaded HTTP REST API (`/opt/spirit/gateway`)      │
│  - Async WebSocket Broadcast Hub (port 8081)           │
│  - Safety Interceptor (Z feedrate clamped <= 1000)     │
│  - Ping-Pong G-code Stream Engine                      │
└───────────────────────────┬────────────────────────────┘
                            │
                 USB Serial │ 115200 Baud, 8-N-1 (/dev/ttyUSB0)
                            ▼
┌────────────────────────────────────────────────────────┐
│              FluidNC (ESP32 Motion Board)              │
│  - Profile: Water_plot (Firmware PLOTCEI v4.0)         │
│  - Travel Limits: X: 730mm, Y: 300mm, Z: 10mm          │
└────────────────────────────────────────────────────────┘
```

- **Base REST URL:** `http://spirit.local:8080` (or `http://100.117.57.78:8080` / `http://192.168.33.172:8080`)
- **WebSocket URL:** `ws://spirit.local:8081` (or `ws://100.117.57.78:8081` / `ws://192.168.33.172:8081`)
- **CORS:** Enabled (`Access-Control-Allow-Origin: *`, `OPTIONS` preflight supported) for Next.js dev server (`localhost:3000`).

---

### 2. HTTP REST API Reference

#### A. Jobs & Streaming

##### `POST /api/jobs/upload`
Uploads a G-code file or raw text. Sanitizes comments, validates bounds, enforces Z-feed clamping ($Z \le 1000$ mm/min), and caches the job.

- **Request Options:**
  - **Multipart FormData:** Field named `file` (containing `.gcode` file).
  - **JSON:**
    ```json
    {
      "name": "mandala.gcode",
      "gcode": "G0 X-350 Y150\nG1 Z-4.50 F1000\nG1 X-320 Y120 F12000\nG0 Z-0.50 F1000"
    }
    ```
- **Response (200 OK):**
  ```json
  {
    "success": true,
    "job_id": "job_1794228000_a1b2c3",
    "name": "mandala.gcode",
    "total_lines": 4,
    "status": "queued"
  }
  ```

##### `POST /api/jobs/start`
Begins streaming lines of a queued job to FluidNC over USB.

- **Request Body (JSON):**
  ```json
  {
    "job_id": "job_1794228000_a1b2c3" // optional; defaults to most recently uploaded job
  }
  ```
- **Response (200 OK):**
  ```json
  {
    "success": true,
    "job_id": "job_1794228000_a1b2c3",
    "status": "streaming"
  }
  ```

##### `GET /api/jobs`
Lists all queued/cached jobs and active streaming status.

---

#### B. Machine Motion & Control

##### `POST /api/control/hold`
Pauses streaming immediately by sending Grbl feed hold byte `!` (0x21). Decelerates and stops without losing position.

- **Response:** `{"success": true, "status": "held"}`

##### `POST /api/control/resume`
Resumes streaming and motion by sending Grbl cycle start byte `~` (0x7E).

- **Response:** `{"success": true, "status": "running"}`

##### `POST /api/control/abort`
Cancels current job immediately by issuing Grbl soft reset `\x18` (Ctrl-X). Pen lifts to safe height upon recovery.

- **Response:** `{"success": true, "status": "aborted"}`

##### `POST /api/control/jog`
Safe relative jogging move (`$J=G91 G21 ...`). Z feedrate is automatically clamped to 1000 mm/min.

- **Request Body (JSON):**
  ```json
  {
    "x": 10.0,      // relative movement in mm
    "y": 0.0,
    "z": 0.0,
    "feed": 12000   // feedrate in mm/min
  }
  ```
- **Response:** `{"success": true, "status": "jogging"}`

##### `POST /api/control/park`
Lifts pen to safe height (`Z -0.50 F1000`) and rapids (`F12000`) to whichever of the 4 designated bed corners is closest to the current pen position.

- **Corners:**
  - Top-Left: `(-680.0, 280.0)`
  - Top-Right: `(-20.0, 280.0)`
  - Bottom-Left: `(-680.0, 20.0)`
  - Bottom-Right: `(-20.0, 20.0)`
- **Response (200 OK):**
  ```json
  {
    "success": true,
    "park_pos": [-680.0, 280.0]
  }
  ```

##### `POST /api/command`
Dispatches raw Grbl / FluidNC G-code or diagnostic query directly to `/dev/ttyUSB0`.

- **Request Body (JSON):** `{"command": "$H"}`
- **Response (200 OK):** `{"success": true, "response": "ok"}`

---

#### C. Machine Status & Diagnostics

##### `GET /api/status`
Returns real-time telemetry snapshot, active job progress, and water evaporation estimates.

- **Response (200 OK):**
  ```json
  {
    "connected": true,
    "port": "/dev/ttyUSB0",
    "state": "Run",
    "mpos": [-340.50, 142.10, -4.50],
    "feed": 12000,
    "buffer_free": 15,
    "streaming": true,
    "paused": false,
    "active_job": {
      "job_id": "job_1794228000_a1b2c3",
      "name": "mandala.gcode",
      "current_line": 140,
      "total_lines": 580,
      "percent": 24.1,
      "active_command": "G1 X-340.50 Y142.10 F12000"
    },
    "clock_active": false,
    "evap_pct": 32.5,
    "sec_to_next": 42.1
  }
  ```

##### `GET /api/diagnostics`
Queries controller over USB (`$I`, `$G`, `$#`, `$$`) and returns structured hardware limits and settings.

- **Response (200 OK):**
  ```json
  {
    "connected": true,
    "port": "/dev/ttyUSB0",
    "firmware": "PLOTCEI v4.0 (main-b30b4a2-dirty)",
    "machine_name": "Water_plot",
    "travel_limits": { "x": 730.0, "y": 300.0, "z": 10.0 },
    "max_feedrates": { "x": 15000.0, "y": 15000.0, "z": 2500.0 },
    "accelerations": { "x": 1500.0, "y": 1500.0, "z": 600.0 },
    "steps_per_mm": { "x": 200.0, "y": 200.0, "z": 200.0 },
    "modal_state": "G0 G54 G17 G21 G90 G94 M9 T0 F12000",
    "wifi_ip": "192.168.33.3",
    "safe_corners": [
      [-680.0, 280.0],
      [-20.0, 280.0],
      [-680.0, 20.0],
      [-20.0, 20.0]
    ]
  }
  ```

---

#### D. Clock & Motivational Quote Generation

##### `POST /api/clock/toggle`
Enables or disables autonomous clock drawing on the minute cycle.
- **Request Body (JSON):** `{"active": true}`

##### `POST /api/clock/trigger`
Immediately plots the current time and quote on the water surface.

##### `POST /api/clock/config`
Configures clock style, center position, pen heights, and quote rotation.
- **Request Body (JSON):**
  ```json
  {
    "style": "quote",          // "circle" | "square" | "quote"
    "center_x": -350.0,
    "center_y": 150.0,
    "radius": 110.0,
    "z_up": -0.5,
    "z_down": -4.5,
    "feed_xy": 12000,
    "feed_z": 1000,
    "rapid_xy": 12000,
    "mirror_x": true,
    "quote_rotate": true,      // auto-rotates curated quote every minute
    "quote_text": "BREATHE AND BEGIN" // custom quote if quote_rotate is false
  }
  ```

##### `GET /api/preview`
Returns vector path segments (`strokes`) and bounding metadata for immediate preview on an SVG or Canvas element without running the machine.

---

### 3. WebSocket Hub Protocol (`ws://spirit.local:8081`)

#### A. Outgoing Broadcasts (Server -> Client)

##### 1. Machine Telemetry (`type: "telemetry"`)
Broadcast every 200ms:
```json
{
  "type": "telemetry",
  "data": {
    "state": "Run",             // "Idle" | "Run" | "Hold" | "Alarm" | "Disconnected"
    "mpos": [-340.50, 142.10, -4.50],
    "feed": 12000,
    "buffer_free": 15
  }
}
```

##### 2. Job Streaming Progress (`type: "job_progress"`)
Broadcast on every G-code line acknowledged (`ok`):
```json
{
  "type": "job_progress",
  "data": {
    "job_id": "job_1794228000_a1b2c3",
    "name": "mandala.gcode",
    "current_line": 142,
    "total_lines": 580,
    "percent": 24.48,
    "active_command": "G1 X-320.50 Y140.20 F12000",
    "elapsed_seconds": 18.2,
    "estimated_remaining_seconds": 56.4
  }
}
```

##### 3. Job Completed (`type: "job_complete"`)
```json
{
  "type": "job_complete",
  "data": {
    "job_id": "job_1794228000_a1b2c3",
    "status": "success",
    "total_lines": 580,
    "duration_seconds": 74.6
  }
}
```

#### B. Incoming Client Messages (Client -> Server)

Clients can send action commands directly over the open WebSocket connection:
- Pause: `{"action": "hold"}`
- Resume: `{"action": "resume"}`
- Abort: `{"action": "abort"}`
- Jog: `{"action": "jog", "x": 10.0, "y": 0.0, "z": 0.0, "feed": 12000}`
- Raw Command: `{"action": "command", "command": "$H"}`

---

### 4. TypeScript Definitions for Next.js (`types/spirit.d.ts`)

```typescript
export type MachineState = "Idle" | "Run" | "Hold" | "Alarm" | "Disconnected";

export interface TelemetryData {
  state: MachineState;
  mpos: [number, number, number]; // [X, Y, Z]
  feed: number;
  buffer_free: number;
}

export interface JobProgressData {
  job_id: string;
  name: string;
  current_line: number;
  total_lines: number;
  percent: number;
  active_command: string;
  elapsed_seconds?: number;
  estimated_remaining_seconds?: number;
}

export interface JobCompleteData {
  job_id: string;
  status: "success" | "aborted" | "error";
  total_lines: number;
  duration_seconds?: number;
}

export type WebSocketEvent =
  | { type: "telemetry"; data: TelemetryData }
  | { type: "job_progress"; data: JobProgressData }
  | { type: "job_complete"; data: JobCompleteData }
  | { type: "job_error"; data: { job_id: string; error: string } };

export interface MachineDiagnostics {
  connected: boolean;
  port: string;
  firmware: string;
  machine_name: string;
  travel_limits: { x: number; y: number; z: number };
  max_feedrates: { x: number; y: number; z: number };
  accelerations: { x: number; y: number; z: number };
  steps_per_mm: { x: number; y: number; z: number };
  modal_state: string;
  wifi_ip: string;
  safe_corners: [number, number][];
}

export interface ClockConfig {
  style: "circle" | "square" | "quote";
  center_x: number;
  center_y: number;
  radius: number;
  z_up: number;
  z_down: number;
  feed_xy: number;
  feed_z: number;
  rapid_xy: number;
  reset_wco: boolean;
  mirror_x: boolean;
  quote_text: string;
  quote_rotate: boolean;
}
```

---

### 5. Critical Hardware Rules & Safety Safeguards

1. **Z-Axis Speed Limit:** Always $\le 1000$ mm/min (`F1000`). Software automatically clamps all uploaded files.
2. **Buffer Flush:** Use `G4 P0.05` for dwell/sync. Do **not** send Marlin `M400` (causes error:20 on FluidNC).
3. **Pen Heights:** Safe UP = `Z -0.50 mm`. Plotting DOWN = `Z -4.50 mm`.
4. **Closest Corner Parking:** Pen automatically lifts and moves to the nearest of `(-680, 280)`, `(-20, 280)`, `(-680, 20)`, or `(-20, 20)` after completing drawings.
