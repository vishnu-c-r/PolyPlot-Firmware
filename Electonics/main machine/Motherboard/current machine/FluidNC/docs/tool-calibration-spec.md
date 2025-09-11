# Tool Calibration: Configurability and Robustness Plan

This document outlines changes to make Tool Calibration configurable and more robust, while staying backward compatible with the current behavior.

## Goals

- Make tool bank parameters configurable (count, spacing, axis/sign, default Z).
- Allow choosing +/− limit directions per axis for calibration.
- Keep Z default constant for all tools (as per your rack/holder design).
- Add validation/logging when work area config is missing or inconsistent.
- Debounce/confirm limit events without biasing measurements.
- Preserve backward compatibility if new config is absent.

---

## JSON Schema Additions (backward compatible)

File: `FluidNC/data/toolconfig.json`

Add two optional top-level blocks alongside the existing `tools` array:

```json
{
  "tools": [ ... ],
  "rack": {
    "count": 6,
    "spacing": 42.0,
    "spacingAxis": "Y",
    "spacingSign": -1,
    "zDefault": -10.0
  },
  "calibration": {
    "moveDirX": "+",
    "moveDirY": "+",
    "mapSignX": "-",
    "mapSignY": "-"
  }
}
```

Defaults when keys are missing:

- `rack`: count=6, spacing=42.0, spacingAxis="Y", spacingSign=-1, zDefault=-10.0
- `calibration`: moveDirX="+", moveDirY="+", mapSignX="-", mapSignY="-"

Rationale:

- `moveDir*` selects which direction to move to reach the endstop (independent of homing).
- `mapSign*` controls how limit positions map into tool coordinates; current code corresponds to `"-"` for X and Y.
- `spacingAxis`/`spacingSign` define how additional tools are laid out from tool 1.

---

## Code Touchpoints and Changes

### A) WebUI::ToolConfig

Files: `FluidNC/src/WebUI/ToolConfig.{h,cpp}`

1) Parse optional `rack` and `calibration` blocks with defaults.
2) Add getters (no breaking changes):
   - Rack: `getRackCount()`, `getRackSpacing()`, `getRackAxis()`, `getRackSpacingSign()`, `getRackZDefault()`
   - Calibration: `getCalMoveDirX()`, `getCalMoveDirY()`, `getMapSignX()`, `getMapSignY()`
3) Leave existing `tools` parsing untouched.

### B) ToolCalibration

File: `FluidNC/src/ToolCalibration.cpp`

1) Replace hardcoded constants with reads from `ToolConfig`:
   - TOOL_BANK_COUNT -> `cfg.getRackCount()`
   - TOOL_BANK_SPACING -> `cfg.getRackSpacing()`
   - TOOL_BANK_Z_DEFAULT -> `cfg.getRackZDefault()`

2) Movement direction per axis:
   - Replace `planAxisTowardPositiveLimit(axis)` with `planAxisTowardLimit(axis, dirSign)`
   - `dirSign` derived from `getCalMoveDirX/Y()` (`+` => +1, `-` => -1)
   - Distance remains `abs(maxTravel) * 1.2f * dirSign`

3) Mapping signs in `finishCalibration()`:
   - Current mapping is equivalent to `mapSignX = "-"`, `mapSignY = "-"`.
   - General formula:
     - `tool1x = sign(mapSignX) * (limitX - originX - pulloffX)`
     - `tool1y = sign(mapSignY) * (limitY - originY - pulloffY)`

4) Populate tools 2..N based on `rack.spacingAxis` and `rack.spacingSign`:
   - If `axis == 'Y'`: `y = tool1y + spacingSign * (n - 1) * spacing`
   - If `axis == 'X'`: `x = tool1x + spacingSign * (n - 1) * spacing`

5) Validation and logging:
   - At start and in `finishCalibration()`:
     - If `config->_workArea` is missing/disabled, warn and:
       - Use `_startMpos` fallback for mapping or skip origin move.
     - Log chosen calibration directions, map signs, rack layout, and computed tool1.
   - Clamp `rack.count` to `ToolConfig::MAX_TOOLS` if desired.

6) Debounce/settle on limit:
   - Keep stage/axis gating (already present).
   - Add short software debounce window (e.g., ignore same-axis events < 10–20 ms apart).
   - After first edge: `Stepper::reset(); plan_reset();` then a brief settle (~5–10 ms) or wait for idle before reading `mpos`.
   - Do NOT re-approach; avoid biasing the measurement. A future “high-precision" mode can add retract/slow re-approach.

---

## Pseudocode Snippets (for reference)

- Movement direction:

```cpp
int dirX = (cfg.getCalMoveDirX() == '+') ? +1 : -1;
int dirY = (cfg.getCalMoveDirY() == '+') ? +1 : -1;

planAxisTowardLimit(X_AXIS, dirX);
// later
planAxisTowardLimit(Y_AXIS, dirY);
```

- Mapping signs:

```cpp
int mapX = (cfg.getMapSignX() == '+') ? +1 : -1;
int mapY = (cfg.getMapSignY() == '+') ? +1 : -1;

float tool1x = mapX * (xLimit - originX - xPul);
float tool1y = mapY * (yLimit - originY - yPul);
```

- Tool population:

```cpp
for (int n = 2; n <= rackCount; ++n) {
  Tool t = {n, tool1x, tool1y, zDefault, false};
  if (rackAxis == 'Y') t.y += spacingSign * (n - 1) * spacing;
  else                 t.x += spacingSign * (n - 1) * spacing;
  addOrUpdate(t);
}
```

---

## Acceptance Criteria / Tests

- Back-compat:
  - With existing `toolconfig.json` (no `rack`/`calibration` blocks), behavior matches current firmware: count=6, spacing=42, -Y layout, default Z=-10, +limit moves, negative-space mapping.

- Configurable count/spacing:
  - Changing `rack.count` and `rack.spacing` results in the expected number of tools and positions.

- Axis and sign control:
  - Toggling `calibration.moveDirX/Y` causes motions toward the corresponding limit direction.
  - Toggling `calibration.mapSignX/Y` flips the computed tool1 coordinates accordingly.

- Validation/logging:
  - Missing/disabled work area logs a warning and uses fallback without crash.
  - A summary log prints chosen directions, signs, rack config, and final tool1.

- Debounce:
  - Spurious duplicate limit events within the debounce window don’t corrupt state.
  - Captured `mpos` after a limit is stable (no mid-decel reads).

---

## Migration / Rollout

1) Land code changes with defaults preserving current behavior.
2) Optionally add the `rack` and `calibration` blocks to `toolconfig.json` to customize per machine.
3) (Later) Expose these fields in the Web UI with validation and helper text.

---

## Notes / Non-Goals

- Per-tool Z offsets are intentionally not implemented; `rack.zDefault` applies to all tools. `setToolZ()` can still override tool 1 if needed.
- No change to homing; calibration direction can be bound to homing direction in the future as a default.

---

## Open Questions

- Should `rack.count` be hard-capped at `MAX_TOOLS` or allowed to exceed and only display up to `MAX_TOOLS`?
- Preferred debounce duration (10–20 ms suggested)?

---

## Next Steps

- Implement `ToolConfig` parsing + getters.
- Update `ToolCalibration` to consume config and add debounce/settle.
- Add concise logs and parameter dump on calibration finish.
