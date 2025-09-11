#pragma once

#include "Error.h"
#include "Config.h"
#include "NutsBolts.h"
#include "WebUI/Commands.h"
#include "Machine/LimitPin.h"

namespace WorkAreaCalibration {
    // Start a calibration pass (1 or 2). Both passes move X then Y toward the axis homing direction limit.
    void startPass(int pass);

    // Limit ISR callback from Protocol when calibration is active
    void onLimit(Machine::LimitPin* limit);

    bool isCalibrating();
    void abortCalibration();
}
