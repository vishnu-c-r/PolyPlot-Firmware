#pragma once

#include "Machine/LimitPin.h"

namespace WorkAreaCalibration {
    void start(int mode = 0);  // mode: 0=default(max-0.02), 1=max bounds, 2=min bounds
    void onLimit(Machine::LimitPin* limit);
    bool isRunning(); 
    // Save current config->_workArea values to config.yaml and restart
    void saveWorkAreaToConfigAndRestart();
    void abort();
    
    // Interactive sequence helpers for M156 P1/P2/P3
    // P1: auto-measure from max XY (seek to positive limits, capture max bounds; no reboot)
    void startCaptureMax();
    // P2: auto-measure from min XY (seek to negative limits, capture min bounds; no reboot)
    void startCaptureMin();
    // P3: compute origin = max-0.02, persist bounds+origin to config.yaml and reboot
    void finalizeAndSave();
}
