# Calibration Guide

This guide covers two built-in calibration workflows:

- Tool bank calibration (M155)
- Work area and origin calibration (M156)

Both flows are designed to run without homing, use controlled “system motion,” and avoid soft-limit alarms during the routine while still respecting hard limits.

## Prerequisites and safety

- Ensure X and Y positive limit switches are wired and reliable.
- Clear the machine of obstructions and secure the pen/tool.
- It’s okay if the machine isn’t homed; these flows use measured positions and controlled motion.

## Tool calibration (M155)

Purpose: Measure Tool 1’s X/Y location from the positive limits, map into negative-space coordinates relative to the current work origin, and auto-populate a bank of tools with consistent spacing. Optionally set Z.

What it does

- Seeks X+ to the positive limit, then Y+ to the positive limit.
- Captures both limit positions, compensates each with axis pulloff, and maps into negative-space relative to the configured work origin (origin_x, origin_y).
- Saves Tool 1 exactly (no extra rounding) and auto-generates tools 2–6 along −Y with fixed spacing.
- Returns to the work origin when finished.

How to run

- Send M155 to start the calibration.
- After calibration, to set/update Tool 1 Z: send M155 Zn.n (e.g., M155 Z-10.0). This can be done any time after M155 finishes.

Persistence

- Tools are stored in the web tool configuration JSON and persist across restarts.

Notes

- Soft limits are temporarily bypassed for these system motions only. Hard limits still stop motion if configured.
- If you abort/reset during calibration, the routine is canceled and the machine returns to Idle.

## Work area and origin calibration (M156)

Purpose: Measure the reachable work area in XY, compute a robust work origin in negative space, round values to 0.1 mm to avoid floating artifacts (e.g., −30.19999), write directly to config.yaml, and restart.

What it does

- Seeks X+ to the positive limit, then Y+ to the positive limit.
- Records both limit positions and compensates with axis pulloff.
- Computes origin_x/origin_y in negative space from the measured max positions.
- Computes min_x/min_y based on travel from the start position to the captured max; sets max_x/max_y slightly inside origin (origin − 0.3) to maintain a soft-limit guard.
- Rounds all work area values to one decimal (0.1 mm) to avoid float artifacts.
- Atomically updates the active config file (resolves the correct local FS and `config_filename`) and restarts the controller.

How to run

- Move the machine to your preferred “work origin reference” position, then send M156.
- The controller will move, update config.yaml, and restart to apply the changes.

Persistence

- Updates `work_area` in the active `config.yaml` on the local filesystem (SPIFFS or LittleFS), using a `.new` file and rename for atomicity.

Notes

- The min bounds are derived from your start position relative to the measured positive limits. Start from a consistent reference point for repeatable results.
- Values are printed with one decimal in the config to eliminate soft-limit errors caused by tiny float residue.

## Soft-limit behavior during calibration

- Calibration system motions are queued with limits pre-checked so soft limits won’t trigger mid-routine.
- Actual limit trips during calibration are routed to the active calibration module (Tool or Work Area) instead of raising an alarm.
- Outside of calibration and pen-change logic, soft limits are enforced normally by the Limits subsystem.

## Troubleshooting

- Calibration won’t start: Ensure the machine is Idle (not in Hold/Cycle/SafetyDoor) and no emergency stop is active.
- Hard limit alarm: Check wiring or clearance; hard limits still stop motion if enabled.
- Values not updated: Verify local filesystem free space and that `config_filename` points to a writable file. Re-run M156.
- Float artifacts in config: M156 writes rounded values (one decimal). If you edited the file manually, run M156 to normalize.

## Reference

- Tool calibration command: M155
- Set Tool 1 Z after calibration: M155 Z-10.0 (example)
- Work area and origin calibration: M156
