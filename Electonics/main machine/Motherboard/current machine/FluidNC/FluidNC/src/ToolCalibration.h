#pragma once

// Copyright (c) 2023 -    Vishnu
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Error.h"
#include "Config.h"
#include "NutsBolts.h"
#include "WebUI/ToolConfig.h"
#include "Machine/LimitPin.h"  // For limit callback

namespace ToolCalibration {
    // Lightweight namespace-level API for a simple two-axis tool dock calibration.
    // Usage (high-level):
    //  - Call startCalibration() to begin. The current machine position is captured as
    //    the "hand-placed" starting point. The routine will move X toward its limit
    //    until a limit pin trip is detected, capture the mpos, then repeat for Y.
    //  - The limit callback `onLimit()` is called from the protocol limit handler.

    // Start the calibration sequence (moves X then Y). Soft-limits for X/Y are
    // temporarily disabled while the procedure runs.
    void startCalibration();

    // Called from the realtime limit handler when a limit pin trips. If a
    // calibration is in progress this will capture the position and advance the
    // calibration sequence.
    void onLimit(Machine::LimitPin* limit);

    // Returns true while calibration is in progress
    bool isCalibrating();

    // Abort calibration and restore machine state
    void abortCalibration();

    // Set or update tool Z (can be issued via M155 Z...). If calibration is
    // active it stores pending Z; otherwise updates tool1 immediately.
    void setToolZ(float z);


} // namespace ToolCalibration
