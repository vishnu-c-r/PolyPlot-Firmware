# Firmware Modifications Documentation

**Target Audience:** Junior Engineers / New Developers  
**Firmware:** FluidNC (ESP32 CNC Firmware)  
**Platform:** ESP32 (8MB Flash, 1-9MB LittleFS)  
**Last Updated:** November 2025

This document provides a comprehensive guide to the custom modifications made to the FluidNC firmware for the PolyPlot plotter controller. It covers the base firmware architecture, custom features, build configuration, and how to replicate and maintain the codebase.

---

## 1. Overview of Customizations

The firmware has been heavily modified to support:

- **Automatic Tool Changing (ATC)** with persistent state tracking.
- **Work Area Calibration** (3-step M156 workflow: P1, P2, P3).
- **Tool Calibration** (M155 command with automatic homing).
- **Laser Pointer Offsets** (M150/M151 commands).
- **State Persistence** across restarts (SPIFFS JSON files: `calib_temp.json`, `toolconfig.json`, `penstate.json`).
- **Pull-off Compensation** to account for hard-limit switches' physical offset.
- **Auto-detection of Homing Direction** to correctly calculate axis boundaries.

---

## 2. Base Firmware Architecture

### 2.1 What is FluidNC?

**FluidNC** is a modern CNC firmware optimized for the ESP32 microcontroller. It replaces older firmware like Grbl_ESP32. Key characteristics:

- **Object-Oriented Design**: Hierarchical, modular structure.
- **Hardware Abstraction**: Easy to support different motors, spindles, and stepper drivers.
- **Web-Based UI**: Built-in browser interface (no external software needed).
- **Configuration-Driven**: Machine behavior is defined in `config.yaml` (not hardcoded).
- **Multi-Protocol Support**: Standard G-code compatibility with custom commands (M156, M155, etc.).

### 2.2 ESP32 Memory Model

The ESP32 used has **8MB of Flash** memory, configured as:

| Component | Size | Purpose |
|-----------|------|---------|
| **Firmware (app)** | 3MB | Compiled FluidNC binary |
| **Partition Table** | 64KB | Boot partitions |
| **Filesystem (LittleFS)** | 1MB (or more) | SPIFFS for config.yaml, calibration data, web UI assets |

**Configuration:** Defined in `platformio.ini` under `board_build.partitions`:

```ini
board_build.partitions = FluidNC/ld/esp32/app3M_spiffs1M_8MB.csv
```

To increase filesystem size (e.g., 9MB LittleFS on a 16MB ESP32):

```ini
board_build.partitions = FluidNC/ld/esp32/app3M_spiffs9M_16MB.csv
board_upload.flash_size = 16MB
```

### 2.3 Core Firmware Modules

#### **Protocol** (`Protocol.h`, `Protocol.cpp`)

- **Purpose:** Main communication layer between the machine and the user (USB serial, WiFi, Telnet).
- **Functions:**
  - `protocol_main_loop()`: Infinite loop that reads commands from serial/WiFi and executes them.
  - `protocol_execute_realtime()`: Handles real-time commands (feed hold, cycle start, etc.).
  - `protocol_buffer_synchronize()`: Blocks until all buffered motion is complete.
- **Key Concept:** The protocol layer decouples user input from motion execution.

#### **GCode Parser** (`GCode.h`, `GCode.cpp`)

- **Purpose:** Parses G-code commands (e.g., `G0 X10 Y20`, `M156 P1`).
- **Workflow:**
  1. User sends a G-code line (e.g., `M156 P1`).
  2. Parser extracts words (M, P, X, Y, etc.).
  3. Parser dispatches to appropriate handler (motion, ATC, calibration).
- **Modified for Custom Commands:**
  - Added `M156` (Work Area Calibration).
  - Added `M155` (Tool Calibration).
  - Added `M150`/`M151` (Laser Offsets).
- **Key Struct:** `gc_block` holds parsed G-code parameters.

#### **Planner** (`Planner.h`, `Planner.cpp`)

- **Purpose:** Acceleration planning; converts target position + feed rate into smooth motion profile.
- **Key Functions:**
  - `plan_buffer_line(float* target, plan_line_data_t* pl_data)`: Queue a linear motion.
  - `plan_reset_buffer()`: Clear the motion queue.
  - `plan_get_current_block()`: Fetch the next motion block to execute.
- **Internal:** Uses a ring buffer (`block_buffer`) to store pending motions.
- **Structures:**
  - `plan_line_data_t`: Input data for a line motion (target position, feed rate, motion flags).
  - `plan_block_t`: Internal representation of a motion block (step counts, acceleration, timings).
- **Custom Field:** Added `currentPenNumber` and `previousPenNumber` for tool change tracking.

#### **Motion Control** (`MotionControl.h`, `MotionControl.cpp`)

- **Purpose:** Translates G-code motion commands into planner calls.
- **Key Functions:**
  - `mc_linear()`: Execute linear motion (G0, G1).
  - `mc_arc()`: Execute arc motion (G2, G3).
  - `mc_dwell()`: Execute dwells (G4).
  - `mc_pen_change()`: Execute tool/pen change (M6).
  - `mc_move_motors()`: Direct motor movement.
- **State Persistence:** Saves pen state to `penstate.json` after tool changes.
- **Integration Points:**
  - Calls the **Planner** to queue motions.
  - Calls **Limits** to check soft limits.
  - Calls **Homing** to auto-home after calibration.

#### **Stepper Driver** (`Stepper.h`, `Stepper.cpp`)

- **Purpose:** Low-level motor pulse generation via interrupt/ISR.
- **Mechanism:** Reads motion blocks from the planner and generates step/direction pulses on GPIO pins.
- **Integration:** Uses ESP32 RMT (Remote Control) peripheral for timing-critical operations.
- **Not Typically Modified** for calibration logic.

#### **Limits & Homing** (`Limits.h`, `Limits.cpp`, `Machine/Homing.h`, `Machine/Homing.cpp`)

- **Purpose:** Enforce soft/hard limits and home the machine to a known position.
- **Key Functions:**
  - `Machine::Homing::run_cycles()`: Execute homing sequence to find limit switches.
  - `limits_init()`: Initialize limit switch pins.
  - `check_soft_limits()`: Verify motion is within `min_x`, `max_x`, `min_y`, `max_y`.
- **Called By:** Calibration routines (M155, M156) to auto-home after completion.

### 2.4 Data Flow: From G-Code to Motor Pulses

```
User sends "G0 X50 Y30" via Serial/WiFi
    ↓
Protocol reads the line and queues it
    ↓
GCode parser extracts: Motion=G0, X=50, Y=30
    ↓
MotionControl.mc_linear() is called
    ↓
Limits.check_soft_limits() verifies X=50, Y=30 are valid
    ↓
Planner.plan_buffer_line() queues motion block (acceleration profile computed)
    ↓
Stepper ISR executes the block (generates step/direction pulses)
    ↓
Motors move to (50, 30)
```

---

## 3. Key Features & Implementation

## 3. Custom Features & Implementation

### 3.1 Work Area Calibration (`M156`)

**Files:** `src/WorkAreaCalibration.cpp`, `src/WorkAreaCalibration.h`, `src/GCode.cpp`

**Workflow:**
The calibration is a 3-step process to define the machine's usable area based on physical hard limits.

1. **`M156 P1`**: Seeks positive limits (Max X, Max Y).
    - Measures the machine position at the limit.
    - Saves the "Pass 1" data to `/spiffs/calib_temp.json`.
2. **`M156 P2`**: Seeks negative limits (Min X, Min Y).
    - Measures the machine position at the limit.
    - Saves the "Pass 2" data to `/spiffs/calib_temp.json`.
3. **`M156 P3`**: Calculation & Commit.
    - Loads data from `calib_temp.json`.
    - Calculates the `min_x`, `max_x`, `min_y`, `max_y` based on the two passes.
    - **Auto-detects homing direction**: If measurements are positive, assumes Positive Homing (Max Limit). If negative, assumes Negative Homing (Min Limit).
    - **Pull-off Compensation**: Adjusts the bounds by the configured pull-off distance so the soft limits are correctly placed inside the hard limits.
    - **Origin Offset**: Adds a `0.1mm` inset (`ORIGIN_INSET`) to avoid floating-point rounding errors at the boundary.
    - Updates `config.yaml` in-place.
    - Restarts the MCU.

**Key Logic:**

- `saveCalibrationState()` / `loadCalibrationState()`: Handles intermediate persistence.
- `computeAxis`: Logic to determine min/max/origin from the two passes.

### 2.2 Tool Calibration (`M155`)

**Files:** `src/ToolCalibration.cpp`, `src/ToolCalibration.h`, `src/GCode.cpp`

**Workflow:**

- `M155` triggers an automatic sequence to measure the position of Tool 1.
- It seeks limits, calculates the tool position relative to the work area, and saves it to `/spiffs/toolconfig.json`.
- Automatically generates positions for subsequent tools based on a fixed spacing (`TOOL_BANK_SPACING`).
- **Homing**: Triggers a machine homing cycle (`Machine::Homing::run_cycles`) after calibration completes.

### 2.3 Tool Change (`M6`)

**Files:** `src/GCode.cpp`, `src/MotionControl.cpp`

**Workflow:**

- The standard `M6` command is intercepted in `GCode.cpp`.
- It calls `mc_pen_change` in `MotionControl.cpp`.
- **Persistence**: The current tool index is saved to `/spiffs/penstate.json` whenever a tool is picked up or dropped. This ensures the machine remembers which tool it holds even after a restart.

### 2.4 Laser Offset (`M150` / `M151`)

**Files:** `src/GCode.cpp`

- `M150`: Applies a coordinate offset to align the laser pointer with the toolhead.
- `M151`: Removes the offset.

## 3. File-by-File Modification Summary

### `src/GCode.cpp`

- **Command Interception**: Added cases in the `M` command switch statement for:
  - `M155`: Tool Calibration.
  - `M156`: Work Area Calibration (updated to accept `P` parameter).
  - `M150`/`M151`: Laser offsets.
- **Validation**: Updated `M156` validation to allow `P3` argument.

### `src/WorkAreaCalibration.cpp`

- **Complete Rewrite**: This file contains the custom state machine for the 3-step calibration.
- **JSON Handling**: Uses `WebUI::JSONencoder` to serialize pass data.
- **Config Update**: Contains logic to parse and patch `config.yaml` directly on the SPIFFS filesystem.

### `src/ToolCalibration.cpp`

- **Homing**: Added call to `Machine::Homing::run_cycles(Machine::Axes::homingMask)` at the end of `finishCalibration()`.

### `src/MotionControl.cpp`

- **State Saving**: Added `toolConfig.saveCurrentState()` calls within the tool change logic to persist the active pen index.

### `src/WebUI/JSONEncoder.cpp` & `.h`

- Helper class used for generating JSON for the persistence layer.

## 4. Recent Changes (Current Session)

The following specific changes were made to fix reported issues:

1. **Work Area Calibration (`M156`)**:
    - **P3 Implementation**: Added logic to handle `M156 P3` to calculate and commit settings.
    - **Persistence**: Implemented `calib_temp.json` to store data between P1 and P2 passes (surviving restarts if necessary).
    - **Homing Removal**: Removed automatic homing after P1 and P2 passes (user request).
    - **Origin Offset**: Changed `ORIGIN_INSET` from `0.02f` to `0.1f` to ensure the origin is strictly inside the limit (e.g., -30.1 vs -30.0).
    - **Pull-off Compensation**: Updated calculation to account for the pull-off distance defined in `config.yaml`.
    - **Sign Correction**: Added logic to auto-detect if the machine is homing to positive or negative limits to ensure `min_x`/`max_x` signs are correct.

2. **Tool Calibration (`M155`)**:
    - Added automatic homing after the calibration sequence completes.

3. **Pen State**:
    - Ensured `penstate.json` is updated immediately upon tool change.

4. **Compilation Fixes**:
    - Fixed namespace issues with `WebUI::JSONencoder`.
    - Fixed method calls (`begin_member_object` vs `begin_object`).

## 5. Getting Started (For Developers)

### 5.1 Building the Firmware

1. Open this folder in **VS Code**.
2. Ensure the **PlatformIO** extension is installed.
3. Click the PlatformIO icon (Alien head) in the sidebar.
4. Under **Project Tasks**, select the environment `env:wifi` (default).
5. Click **Build**.

### 5.2 Flashing the Firmware

1. Connect the ESP32 via USB.
2. Under **Project Tasks** -> `env:wifi`, click **Upload**.
3. To upload the filesystem (for `index.html.gz` etc.), click **Upload Filesystem Image**.

### 5.3 Debugging

- Use the Serial Monitor (baud rate 115200) to see debug output.
- Calibration logs will appear when running `M156`.

---

## 6. WiFi & Web UI

### 6.1 WiFi System (`src/WebUI/WifiConfig.h`, `src/WebUI/WifiConfig.cpp`)

The WiFi system allows the machine to be controlled over a network.

**Features:**

- **Dual Mode:** Station (client) mode to connect to existing WiFi, or Access Point (AP) mode to broadcast its own WiFi.
- **Default Credentials:**
  - AP SSID: `PenPlotter`
  - AP Password: `12345678`
  - AP IP: `192.168.0.1`
- **Persistent Configuration:** WiFi settings are stored in YAML config file.

**Key Functions:**

- `WiFiConfig::begin()`: Initialize WiFi.
- `WiFiConfig::StartSTA()`: Connect to external WiFi.
- `WiFiConfig::StartAP()`: Create WiFi access point.
- `WiFiConfig::StopWiFi()`: Turn off WiFi to save power.

### 6.2 Web UI Server (`src/WebUI/WebServer.h`, `src/WebUI/WebServer.cpp`)

The web UI runs an HTTP server on port 81 and WebSocket server on port 82.

**Features:**

- Real-time machine status display.
- G-code sender for remote control.
- File upload for new `config.yaml`.
- SPIFFS filesystem browser.

**Key Endpoints:**

- `GET /api/config` - Fetch machine configuration.
- `POST /api/command` - Send G-code command.
- `WS://192.168.0.1:82` - WebSocket for real-time updates.

### 6.3 WiFi Configuration in `config.yaml`

Example `config.yaml` WiFi section:

```yaml
wifi:
  mode: sta  # "sta" for Station, "ap" for Access Point
  ssid: "MyNetwork"
  ip_address: 192.168.1.100
  gateway: 192.168.1.1
  netmask: 255.255.255.0
  dhcp: false
```

---

## 7. Build Configuration (`platformio.ini`)

### 7.1 Overview

`platformio.ini` is the main build configuration file. It defines:

- Compiler flags
- Target board (ESP32)
- Memory partitions
- Libraries
- Build environments

### 7.2 Memory Partitioning

The ESP32 has limited flash memory. The firmware is configured to split it into:

```ini
[common_esp32_base]
board_build.partitions = FluidNC/ld/esp32/app3M_spiffs1M_8MB.csv
board_upload.flash_size = 8MB
```

This means:

- **3MB** for the firmware binary (app).
- **1MB** for LittleFS filesystem (SPIFFS).
- **4MB** reserved/other.

**To change memory allocation** (e.g., for larger filesystem or 16MB ESP32):

```ini
# For 16MB ESP32 with 9MB SPIFFS
board_build.partitions = FluidNC/ld/esp32/app3M_spiffs9M_16MB.csv
board_upload.flash_size = 16MB
```

### 7.3 Key Environment Configurations

| Environment | Features | Use Case |
|-------------|----------|----------|
| `env:wifi` | WiFi + Web UI | Production (default) |
| `env:bt` | Bluetooth only | Wireless without WiFi |
| `env:wifibt` | WiFi + Bluetooth | Maximum connectivity |
| `env:noradio` | No WiFi/Bluetooth | Minimal memory footprint |
| `env:debug` | Debug symbols, logging | Development |

**To build for a specific environment:**

```bash
platformio run -e env:debug
```

### 7.4 Critical Build Flags

In `platformio.ini`:

```ini
build_flags =
    !python git-version.py
    -DCORE_DEBUG_LEVEL=0          # 0=No logs, 2=Verbose logs
    -DENABLE_WIFI                 # Enable WiFi
    -DUSE_LITTLEFS                # Use LittleFS filesystem
    -std=gnu++17                  # C++17 standard
    -Wno-unused-variable          # Suppress warnings
```

**To enable debug logging:**

```ini
build_flags = -DCORE_DEBUG_LEVEL=2
```

### 7.5 S3 Variant

For ESP32-S3 (newer variant):

```ini
[env:wifi_s3]
extends = common_esp32_s3
lib_deps = ${common.lib_deps} ${common.wifi_deps}
build_flags = ${common_esp32.build_flags} ${common_wifi.build_flags}
```

---

## 8. File-by-File Modification Summary

### Core Firmware Files (Not Modified, Reference Only)

These files are part of the base FluidNC and provide fundamental functionality:

| File | Purpose |
|------|---------|
| `src/Protocol.h`, `Protocol.cpp` | Serial/WiFi communication protocol |
| `src/GCode.h`, `GCode.cpp` | G-code parser (heavily modified for M156, M155, M150/M151, M6T(x)) |
| `src/Planner.h`, `Planner.cpp` | Motion acceleration planning |
| `src/MotionControl.h`, `MotionControl.cpp` | Motion execution (modified for pen change state) |
| `src/Stepper.h`, `Stepper.cpp` | Low-level motor pulse generation |
| `src/Limits.h`, `Limits.cpp` | Soft/hard limit checking |
| `src/Machine/Homing.h`, `Homing.cpp` | Auto-homing routine |
| `src/WebUI/WifiConfig.h`, `WifiConfig.cpp` | WiFi configuration and control |
| `src/WebUI/WebServer.h`, `WebServer.cpp` | HTTP/WebSocket server |
| `src/Main.cpp` | Entry point; initializes all subsystems |

### Custom Files (Heavily Modified)

| File | Purpose | Changes |
|------|---------|---------|
| `src/WorkAreaCalibration.cpp`, `.h` | 3-step work area measurement | Full custom implementation; JSON I/O |
| `src/ToolCalibration.cpp`, `.h` | Tool position calibration | Custom logic; auto-homing at end |
| `src/GCode.cpp` | G-code command parsing | Added M156, M155, M150, M151 cases |
| `src/MotionControl.cpp` | Tool change handler | Added pen state persistence |
| `src/WebUI/ToolConfig.h`, `ToolConfig.cpp` | Tool state persistence | Saves/loads `penstate.json` |
| `src/WebUI/JSONEncoder.cpp`, `.h` | JSON serialization helper | Used by calibration routines |

---

## 9. Configuration Files (SPIFFS)

The ESP32 stores these files in LittleFS (on-chip filesystem):

| File | Purpose | Location |
|------|---------|----------|
| `config.yaml` | Machine definition (motors, limits, WiFi, etc.) | `/spiffs/config.yaml` |
| `toolconfig.json` | Tool positions (from M155 calibration) | `/spiffs/toolconfig.json` |
| `calib_temp.json` | Temporary calibration data (P1/P2 passes) | `/spiffs/calib_temp.json` |
| `penstate.json` | Current tool/pen index | `/spiffs/penstate.json` |
| `index.html.gz` | Web UI (pre-gzipped) | `/spiffs/index.html.gz` |

**Example `calib_temp.json`:**

```json
{
  "pass1": {
    "x": 245.0,
    "y": 185.0
  },
  "pass2": {
    "x": -0.5,
    "y": -0.3
  }
}
```

---

## 10. Recent Changes (Current Session)

The following specific changes were made to fix reported issues:

### 10.1 Work Area Calibration (`M156`)

- **P3 Implementation:** Added logic to handle `M156 P3` to calculate and commit settings.
- **Persistence:** Implemented `calib_temp.json` to store data between P1 and P2 passes (survives restarts if necessary).
- **Homing Removal:** Removed automatic homing after P1 and P2 passes (user request).
- **Origin Offset:** Changed `ORIGIN_INSET` from `0.02f` to `0.1f` to ensure the origin is strictly inside the limit (e.g., -30.1 vs -30.0).
- **Pull-off Compensation:** Updated calculation to account for the pull-off distance defined in `config.yaml`.
- **Sign Correction:** Added logic to auto-detect if the machine is homing to positive or negative limits to ensure `min_x`/`max_x` signs are correct.

### 10.2 Tool Calibration (`M155`)

- Added automatic homing after the calibration sequence completes.

### 10.3 Pen State

- Ensured `penstate.json` is updated immediately upon tool change.

---

## 11. Troubleshooting & Common Issues

### Issue: "Calibration fails with floating-point error"

**Cause:** Origin too close to the hard limit; rounding errors.  
**Solution:** Increase `ORIGIN_INSET` in `WorkAreaCalibration.cpp`.

### Issue: "Max X / Max Y out of bounds"

**Cause:** Homing direction not auto-detected correctly.  
**Solution:** Check if measurement values in P1/P2 are positive or negative; inspect `computeAxis()` logic.

### Issue: "WiFi won't connect"

**Cause:** Incorrect WiFi config in `config.yaml`.  
**Solution:** Check SSID, password, and WiFi mode (Station vs AP).

### Issue: "LittleFS full, can't save calibration"

**Cause:** Insufficient space for `calib_temp.json`.  
**Solution:** Increase LittleFS size in `platformio.ini` or delete old files from SPIFFS.

---

## 12. Next Steps

1. **Build the firmware** locally using PlatformIO.
2. **Study the data flow**: Trace how a G-code command (e.g., `M156 P1`) flows through Protocol → GCode → WorkAreaCalibration.
3. **Modify `WorkAreaCalibration.cpp`**: Add debug logging to understand calibration behavior.
4. **Add a new G-code command** (e.g., `M157` for a new feature):
   - Add case in `GCode.cpp`.
   - Create new handler file (e.g., `src/NewCalibration.cpp`).
   - Call from `GCode.cpp`.
5. **Persist data to JSON**: Use `WebUI::JSONencoder` to save state.
6. **Test WiFi UI**: Connect via `192.168.0.1` (AP mode) and send commands through the web interface.

---

## Appendix A: Quick Reference

### Build Commands

```bash
# Build for WiFi environment
platformio run -e env:wifi

# Upload firmware
platformio run -e env:wifi --target upload

# Upload filesystem (SPIFFS)
platformio run -e env:wifi --target uploadfs

# Monitor serial output
platformio run -e env:wifi --target monitor

# Clean build
platformio run -e env:wifi --target clean
```

### Common G-code Commands (Custom)

```
M155              # Tool Calibration (auto-finds tool 1, generates other positions)
M156 P1           # Work Area Calibration Pass 1 (seek max X, max Y)
M156 P2           # Work Area Calibration Pass 2 (seek min X, min Y)
M156 P3           # Work Area Calibration Pass 3 (compute and commit)
M150 X10 Y5       # Apply laser offset (X=10mm, Y=5mm)
M151              # Remove laser offset
```

### Useful Serial Commands (at 115200 baud)

```
$H                # Home machine
$Config/Filename=config.yaml  # Load specific config file
$Pins/List        # Show all pin assignments
```

---

## Appendix B: Important Constants

In `WorkAreaCalibration.cpp`:

```cpp
const float ORIGIN_INSET = 0.1f;           // Origin offset from limit (mm)
const float PULLOFF_COMPENSATION = 5.0f;   // Pull-off distance (adjust to machine)
```

In `ToolCalibration.cpp`:

```cpp
const float TOOL_BANK_SPACING = 50.0f;     // Distance between tool positions (mm)
```

---

## 11. Getting Started (For Developers)

### 11.1 Building the Firmware

1. Open this folder in **VS Code**.
2. Ensure the **PlatformIO** extension is installed.
3. Click the PlatformIO icon (Alien head) in the sidebar.
4. Under **Project Tasks**, select the environment `env:wifi` (default).
5. Click **Build**.

### 11.2 Flashing the Firmware

1. Connect the ESP32 via USB.
2. Under **Project Tasks** -> `env:wifi`, click **Upload**.
3. To upload the filesystem (for `index.html.gz` etc.), click **Upload Filesystem Image**.

### 11.3 Debugging

- Use the Serial Monitor (baud rate 115200) to see debug output.
- Calibration logs will appear when running `M156`.
