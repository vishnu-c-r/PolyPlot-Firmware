# Architectural Plan: Next.js <-> Pi Gateway <-> FluidNC Streaming Engine

## 1. System Topology & Data Flow

```
┌────────────────────────────────────────────────────────┐
│                   Next.js Frontend                     │
│  - Drag & Drop .gcode upload                           │
│  - WebSocket client (Auto-reconnect, JSON events)      │
│  - Progress HUD (Line N/Total, %, MPos, State)         │
│  - Controls: Start, Hold (!), Resume (~), Abort (0x18) │
└───────────────────────────┬────────────────────────────┘
                            │
               POST /api/jobs/upload (Multipart or JSON)
               WebSocket ws://<pi-ip>:8080/ws
                            ▼
┌────────────────────────────────────────────────────────┐
│                 Pi Gateway Service                     │
│  Module A: HTTP REST API & Upload Handler              │
│  Module B: WebSocket Broadcast Hub                     │
│  Module C: G-Code Streamer & Safety Interceptor        │
│  Module D: Serial Driver (/dev/ttyUSB0 @ 115200)       │
└───────────────────────────┬────────────────────────────┘
                            │
                 USB Serial │ 115200 Baud, 8-N-1
              (/dev/ttyUSB0)│ Ping-Pong Flow Control
                            ▼
┌────────────────────────────────────────────────────────┐
│              FluidNC (ESP32 Motion Board)              │
│  - Motion Planner & Steppers (X, Y, Z)                 │
│  - Returns 'ok', answers '?' with <State|MPos:...>     │
└────────────────────────────────────────────────────────┘
```

---

## 2. API Contract & Schemas

### A. HTTP REST Endpoints

| Method | Endpoint | Payload | Response | Description |
| :--- | :--- | :--- | :--- | :--- |
| `POST` | `/api/jobs/upload` | `multipart/form-data` (`file`) OR `{ "name": string, "gcode": string }` | `{ "job_id": string, "lines_count": number, "status": "queued" }` | Uploads G-code, validates safety rules, prepares job |
| `POST` | `/api/jobs/start` | `{ "job_id": string }` | `{ "status": "streaming" }` | Starts streaming lines to USB |
| `POST` | `/api/control/hold` | `{}` | `{ "status": "held" }` | Sends `!` immediately to pause motion |
| `POST` | `/api/control/resume`| `{}` | `{ "status": "running" }` | Sends `~` immediately to resume motion |
| `POST` | `/api/control/abort` | `{}` | `{ "status": "aborted" }` | Sends `\x18` (Ctrl-X) to abort stream & reset |
| `POST` | `/api/control/jog`   | `{ "x": float, "y": float, "z": float, "feed": int }` | `{ "status": "jogging" }` | Sends `$J=G91 G21 ...` |
| `GET`  | `/api/status`        | `None` | `{ "state": "Idle", "mpos": [x,y,z], "job": {...} }` | Current snapshot |

### B. WebSocket Telemetry Schema (`ws://<pi>:8080/ws`)

#### 1. Machine Status Broadcast (Every 200ms)
```json
{
  "type": "telemetry",
  "data": {
    "state": "Run",
    "mpos": [-342.15, 148.20, -4.50],
    "feed": 12000,
    "buffer_free": 15
  }
}
```

#### 2. Job Progress Broadcast (Emitted on every line `ok`)
```json
{
  "type": "job_progress",
  "data": {
    "job_id": "job_17941400",
    "current_line": 142,
    "total_lines": 580,
    "percent": 24.48,
    "active_command": "G1 X-320.5 Y140.2 F12000",
    "elapsed_seconds": 18.2,
    "estimated_remaining_seconds": 56.4
  }
}
```

#### 3. Job Completed Event
```json
{
  "type": "job_complete",
  "data": {
    "job_id": "job_17941400",
    "status": "success",
    "duration_seconds": 74.6
  }
}
```

---

## 3. Pi Gateway Modular Architecture

### Module 1: `serial_driver`
- **Responsibilities:**
  - Maintains single persistent file descriptor to `/dev/ttyUSB0`.
  - Sets RTS/DTR low on connect, enforces 1.8s boot delay.
  - Exposes two write channels:
    - *Normal Channel:* Appends `\n`, queues for `ok` / `error:N`.
    - *Real-Time Channel:* Out-of-band single-byte bypass (`?`, `!`, `~`, `0x18`) executed immediately without waiting.
  - Background thread polls `?` every 200ms; parses `<State|MPos:x,y,z|...>` regex and publishes to internal event emitter.

### Module 2: `stream_engine`
- **Responsibilities:**
  - Safety Interceptor:
    - Scans every line for `Z` commands. If `Z` is moved with `F > 1000`, clamps feedrate to `F1000` to prevent motor stall.
    - Strips comments (`;` and `(...)`) and empty lines.
  - Ping-Pong Stream Loop:
    - Sends line $N$.
    - Awaits `ok` or `error:`.
    - If `error:`, logs warning or pauses stream depending on severity.
    - Emits progress event `(N / Total * 100)`.
    - Sends line $N+1$.
  - End of job: sends park command `G0 Z-0.50 F1000` and `G4 P0.05`.

### Module 3: `api_server`
- Express / Fastify (Node.js) or Bottle / FastAPI (Python).
- Hosts REST endpoints and WebSocket server.
- Broadcasts stream engine and serial driver events to all connected web clients.

---

## 4. Next.js Frontend Implementation Guide

### A. Core Hooks & State
1. **`useMachineSocket` Hook:**
   - Connects to `ws://<pi-ip>:8080/ws`.
   - Handles auto-reconnection with exponential backoff.
   - Updates React state: `state`, `mpos`, `jobProgress`.

2. **Components:**
   - `<FileUploadZone />`: Accepts `.gcode`, previews line count, POSTs to `/api/jobs/upload`.
   - `<JobControlHUD />`: Big Start, Pause, Resume, Abort buttons linked to REST endpoints.
   - `<ProgressBar />`: Percentage, line index, remaining time.
   - `<CoordinateDisplay />`: X, Y, Z live DRO (Digital Read Out).

---

## 5. Non-Negotiable Safety Invariants

1. **Z Feedrate Ceiling:** Never allow `Z` movement faster than **`F1000` (1000 mm/min)**. Pi gateway MUST enforce clamping in software even if Next.js submits higher feeds.
2. **Buffer Flush:** Never send `M400` (unsupported by FluidNC). Use `G4 P0.05` to drain motion buffer.
3. **Pen Heights:** Safe Travel: `Z -0.50`, Draw Depth: `Z -4.50`.
4. **Exclusive Port Lock:** Gateway server must hold `/dev/ttyUSB0` exclusively.

