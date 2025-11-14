    # PolyPlot Calibration Guide

    This comprehensive guide covers the two essential calibration workflows for the PolyPlot firmware:

    - **Tool Bank Calibration (M155)** — Automatic Tool Changer setup
    - **Work Area Calibration (M156)** — Interactive workspace boundary setup

    Both workflows use intelligent "system motion" that bypasses soft-limit checks while maintaining hard-limit safety.

    ## Table of Contents

    - [Prerequisites and Safety](#prerequisites-and-safety)
    - [Tool Bank Calibration (M155)](#tool-bank-calibration-m155)
    - [Work Area Calibration (M156)](#work-area-calibration-m156)
    - [Calibration System Behavior](#calibration-system-behavior)
    - [Troubleshooting](#troubleshooting)
    - [Quick Reference](#quick-reference)

    ---

    ## Prerequisites and Safety

    ### Hardware Requirements

    - **Limit Switches**: X+ and Y+ positive limit switches must be properly wired and functional
    - **Machine Clearance**: Remove all obstructions from the machine travel area
    - **Tool Security**: Ensure the pen/tool is properly secured before starting

    ### Safety Notes

    - ⚠️ **Hard limits remain active** during calibration for safety
    - ✅ **No homing required** — calibration works from any starting position
    - ✅ **Soft-limit safe** — system motion bypasses soft-limit checks during calibration
    - 🔄 **Abort safe** — Real-time reset (`Ctrl+X`) cleanly cancels calibration and returns to Idle

    ---

    ## Tool Bank Calibration (M155)

    ### Overview

    The Tool Bank Calibration automatically measures Tool 1's position relative to machine limits and creates a complete tool bank configuration. This is essential for the Automatic Tool Changer (ATC) system.

    ### What It Does

    1. **Seeks to Limits**: Moves to X+ positive limit, then Y+ positive limit
    2. **Measures Position**: Captures limit positions with pulloff compensation
    3. **Coordinate Mapping**: Converts machine coordinates to negative-space relative to work origin
    4. **Tool Bank Generation**: 
    - Saves Tool 1 at the measured position
    - Auto-generates Tools 2-6 with consistent Y-axis spacing
    5. **Return to Origin**: Safely returns to work origin when complete

    ### Step-by-Step Procedure

    #### Initial Calibration (XY Measurement)

    1. **Position Tool 1**: Manually move the carriage so Tool 1 is accessible for measurement
    2. **Clear Obstacles**: Ensure clear path to positive X and Y limits
    3. **Execute Calibration**:
    ```
    M155
    ```
    4. **Monitor Progress**: The machine will:
    - Move to X+ limit and measure position
    - Move to Y+ limit and measure position  
    - Return to work origin
    - Display "Tool calibration complete"

    #### Z-Axis Setup (Post-Calibration)

    After XY calibration, set the Z position for Tool 1:

    1. **Manual Z Positioning**: Move Z-axis to desired tool change height
    2. **Record Z Position**: Note the current Z coordinate
    3. **Update Tool Z**:
    ```
    M155 Z-10.0
    ```
    *(Replace `-10.0` with your desired Z coordinate)*

    ### Generated Tool Bank

    The calibration creates a tool bank with:

    - **Tool 1**: Measured position (exact coordinates)
    - **Tools 2-6**: Auto-generated along negative Y-axis with 10mm spacing
    - **Occupancy Flags**: Tool 1 marked as occupied, others as available

    ### Configuration Storage

    - **File**: `/spiffs/toolconfig.json`
    - **API Access**: `GET/POST /toolconfig`
    - **Persistence**: Survives restarts and power cycles

    Example generated configuration:
    ```json
    {
    "tools": [
        { "number": 1, "x": -30.2, "y": -100.0, "z": -10.0, "occupied": true },
        { "number": 2, "x": -30.2, "y": -110.0, "z": -10.0, "occupied": false },
        { "number": 3, "x": -30.2, "y": -120.0, "z": -10.0, "occupied": false }
    ]
    }
    ```

    ---

    ## Work Area Calibration (M156)

    ### Overview

    Work Area Calibration measures the machine's reachable workspace and establishes a robust coordinate system with properly rounded values to eliminate floating-point artifacts.

    ### What It Does

    1. **Measures Workspace**: Seeks to X+ and Y+ limits to determine maximum travel
    2. **Calculates Origin**: Computes work origin in negative space from measured positions
    3. **Establishes Boundaries**: Sets min/max values with proper guard bands
    4. **Rounds Values**: All coordinates rounded to 0.1mm to prevent float errors
    5. **Updates Configuration**: Atomically writes to `config.yaml` and restarts controller

    ### Step-by-Step Procedure

    #### Three-Phase Interactive Workflow

    The M156 calibration uses a three-step process for maximum control and safety:

    #### Phase 1: Capture Maximum Bounds
    ```
    M156 P1
    ```
    - Seeks to X+ positive limit, then Y+ positive limit
    - Captures and stores maximum reachable positions
    - Applies pulloff compensation for safe working edges

    #### Phase 2: Capture Minimum Bounds  
    ```
    M156 P2
    ```
    - Seeks to X- negative limit, then Y- negative limit
    - Captures minimum reachable positions
    - Calculates total travel range

    #### Phase 3: Finalize and Apply
    ```
    M156 P3
    ```
    - Computes optimal work origin: `origin = max - 0.02mm`
    - Rounds all values to 0.1mm precision
    - Atomically updates `config.yaml`
    - Restarts controller to apply new limits

    ### Configuration Storage

    Updates the `work_area` section in your active `config.yaml`:

    ```yaml
    work_area:
    enabled: true
    min_x: -150.0
    min_y: -200.0
    max_x: -0.3
    max_y: -0.3
    origin_x: -30.0
    origin_y: -100.0
    move_to_origin: true
    ```

    ### Important Notes

    - **Starting Position Matters**: Begin Phase 1 from your desired reference point
    - **Consistent Results**: Always start from the same position for repeatable calibration
    - **Automatic Restart**: Controller restarts after Phase 3 to activate new boundaries

    ---

    ## Calibration System Behavior

    ### System Motion Protection

    During calibration, the firmware uses special "system motion" that:

    - **Bypasses Soft Limits**: Calibration moves won't trigger soft-limit alarms
    - **Respects Hard Limits**: Physical limit switches still provide safety protection
    - **Pre-validates Moves**: System checks moves before queuing to planner

    ### Limit Handling During Calibration

    | State | Limit Hit Behavior |
    |-------|-------------------|
    | **Normal Operation** | Soft limits enforced, hard limits trigger alarm |
    | **Tool Calibration** | Limit hits treated as measurements (no alarm) |
    | **Work Area Calibration** | Limit hits treated as measurements (no alarm) |
    | **Pen Change** | System motion path (soft-limit safe) |

    ### Real-time Reset Safety

    - **Ctrl+X** immediately aborts any active calibration
    - Steppers stop, planner clears, state returns to Idle
    - No partial configurations saved during abort

    ---

    ## Troubleshooting

    ### Calibration Won't Start

    | Problem | Solution |
    |---------|----------|
    | Machine not in Idle state | Wait for current operation to complete or reset |
    | Emergency stop active | Clear emergency stop condition |
    | Previous calibration still active | Send real-time reset (`Ctrl+X`) |

    ### Motion Issues

    | Problem | Solution |
    |---------|----------|
    | Hard limit alarm during calibration | Check limit switch wiring and machine clearance |
    | Machine doesn't move to limits | Verify limit switches are functional |
    | Incorrect travel direction | Check motor direction configuration |

    ### Configuration Issues

    | Problem | Solution |
    |---------|----------|
    | Values not saved after M156 | Check filesystem free space and write permissions |
    | Float artifacts in config (e.g., -30.19999) | Re-run M156 calibration to normalize values |
    | Tool positions incorrect after M155 | Verify work origin is properly set, re-run calibration |

    ### Common Error Messages

    | Error | Meaning | Solution |
    |-------|---------|----------|
    | "Calibration failed: limit not reached" | Limit switch not triggered | Check wiring and switch functionality |
    | "Config write failed" | Cannot write to filesystem | Check available storage space |
    | "Invalid calibration state" | Internal state error | Reset controller and retry |

    ---

    ## Quick Reference

    ### Commands

    | Command | Purpose | When to Use |
    |---------|---------|-------------|
    | `M155` | Tool bank calibration (XY) | Initial ATC setup |
    | `M155 Z-10.0` | Set Tool 1 Z coordinate | After XY calibration |
    | `M156 P1` | Capture maximum bounds | Phase 1 of work area setup |
    | `M156 P2` | Capture minimum bounds | Phase 2 of work area setup |
    | `M156 P3` | Finalize and apply | Phase 3 of work area setup |

    ### Files Created/Modified

    | File | Purpose | Format |
    |------|---------|---------|
    | `/spiffs/toolconfig.json` | Tool bank configuration | JSON |
    | `/spiffs/penstate.json` | Current tool state | JSON |
    | `config.yaml` | Work area boundaries | YAML |

    ### Safety Reminders

    - ✅ Always clear machine of obstructions before calibration
    - ✅ Verify limit switches are functional
    - ✅ Use `Ctrl+X` to safely abort calibration if needed
    - ⚠️ Hard limits remain active for safety during calibration
    - 🔄 Controller automatically restarts after work area calibration

    ---

    *For additional support, refer to the main firmware documentation or check the troubleshooting section above.*