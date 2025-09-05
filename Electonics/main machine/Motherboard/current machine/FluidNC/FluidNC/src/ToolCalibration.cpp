#include "ToolCalibration.h"
#include "Machine/MachineConfig.h"
#include "MotionControl.h"
#include "Protocol.h"
#include "Limits.h"
#include "Report.h"
#include "Planner.h"
#include "WebUI/ToolConfig.h"
#include "System.h"
#include "Machine/Homing.h"
#include <cmath>

namespace ToolCalibration {
    static bool  _isCalibrating = false;
    static int   _stage         = 0;        // 0 = none, 1 = X moving, 2 = Y moving
    static float _startMpos[MAX_N_AXIS];
    static float _xLimitMpos[MAX_N_AXIS];
    static float _yLimitMpos[MAX_N_AXIS];
    static float _pendingZ      = 0.0f;     // user-specified Z capture

    static const int REPORT_LINE_NUMBER = 0;

    // --- Tool bank constants (adjust here as needed) ---
    static constexpr int   TOOL_BANK_COUNT    = 6;        // total tools (1..6)
    static constexpr float TOOL_BANK_SPACING  = 42.0f;    // mm between successive tools along -Y
    static constexpr float TOOL_BANK_Z_DEFAULT = -10.0f;  // hardcoded Z for all tools (within -10.5 +/-0.5 range)
    // Store exact measured floating values (no rounding)

    void loadPulloffFromConfig() {
        // compatibility stub
    }

    static float getAxisPulloff(size_t axis) {
        if (axis < config->_axes->_numberAxis && config->_axes->_axis[axis]) {
            return config->_axes->_axis[axis]->commonPulloff();
        }
        return 0.0f;
    }

    static void moveToOriginAfterCalibration() {
        if (!config->_workArea || !config->_workArea->_enabled) return;
        float* cur = get_mpos();
        float  target[MAX_N_AXIS];
        copyAxes(target, cur);
        target[X_AXIS] = config->_workArea->_originX;
        target[Y_AXIS] = config->_workArea->_originY;
        plan_line_data_t pl = {};
        pl.motion.rapidMotion       = 1;
        pl.motion.noFeedOverride    = 0;
        pl.feed_rate                = 6000.0f; // arbitrary rapid feed
        pl.line_number              = REPORT_LINE_NUMBER;
        pl.limits_checked           = true; // assume within work area
        bool success = config->_kinematics->cartesian_to_motors(target, &pl, cur);
        if (success) {
            log_info("ToolCalibration: Moving to work origin X=" << target[X_AXIS] << " Y=" << target[Y_AXIS]);
            protocol_send_event(&cycleStartEvent);
        } else {
            log_warn("ToolCalibration: Failed to queue move to origin");
        }
    }

    static void planAxisTowardPositiveLimit(size_t axis) {
        float target[MAX_N_AXIS];
        float* current_mpos = get_mpos();
        copyAxes(target, current_mpos);
        float travel_distance = config->_axes->_axis[axis] ? config->_axes->_axis[axis]->_maxTravel * 1.2f : 1000.0f;
        target[axis] += travel_distance; // move positive
        log_info("ToolCalibration: Planning +limit move for axis " << axis << " from " << current_mpos[axis] << " to " << target[axis]);

        plan_line_data_t plan_data = {};
        plan_data.motion                = {};
        plan_data.motion.systemMotion   = 1;
        plan_data.motion.noFeedOverride = 1;
        plan_data.coolant.Mist          = 0;
        plan_data.coolant.Flood         = 0;
        plan_data.line_number           = REPORT_LINE_NUMBER;
        plan_data.is_jog                = false;
        plan_data.feed_rate             = 2000.0f;
        plan_data.limits_checked        = true; // Skip soft limits

        bool success = config->_kinematics->cartesian_to_motors(target, &plan_data, current_mpos);
        if (success) {
            log_info("ToolCalibration: Motion planned successfully, queuing cycle start");
            protocol_send_event(&cycleStartEvent);
        } else {
            log_error("ToolCalibration: Failed to plan motion");
        }
    }

    void startCalibration() {
        if (_isCalibrating) return;
        log_info("ToolCalibration: Starting tool calibration toward +limits...");
        set_state(State::ToolCalibration);

        float* mpos = get_mpos();
        for (size_t i = 0; i < config->_axes->_numberAxis; ++i) _startMpos[i] = mpos[i];

        if (config->_axes->_numberAxis > 0) config->_axes->_axis[X_AXIS]->_softLimits = false;
        if (config->_axes->_numberAxis > 1) config->_axes->_axis[Y_AXIS]->_softLimits = false;

        _isCalibrating = true;
        _stage = 1;
        planAxisTowardPositiveLimit(X_AXIS);
    }

    bool isCalibrating() { return _isCalibrating; }

    void abortCalibration() {
        if (!_isCalibrating) return;
        log_info("ToolCalibration: Calibration aborted");
        if (config->_axes->_numberAxis > 0) config->_axes->_axis[X_AXIS]->_softLimits = true;
        if (config->_axes->_numberAxis > 1) config->_axes->_axis[Y_AXIS]->_softLimits = true;
        _isCalibrating = false;
        _stage = 0;
        set_state(State::Idle);
    }

    static void finishCalibration() {
        if (config->_axes->_numberAxis > 0) config->_axes->_axis[X_AXIS]->_softLimits = true;
        if (config->_axes->_numberAxis > 1) config->_axes->_axis[Y_AXIS]->_softLimits = true;
        float tool1x;
        float tool1y;
        if (config->_workArea) {
            float xPul = getAxisPulloff(X_AXIS);
            float yPul = getAxisPulloff(Y_AXIS);
            // X: keep negative-space mapping (origin referenced, pulloff removed, then negated)
            float diffX = _xLimitMpos[X_AXIS] - config->_workArea->_originX - xPul;
            tool1x      = -diffX;
            // Y: now also map into negative space like X (origin referenced, pulloff removed, then negated)
            float diffY = _yLimitMpos[Y_AXIS] - config->_workArea->_originY - yPul;
            tool1y      = -diffY;
            log_info("ToolCalibration: mapped tool1 (limitX=" << _xLimitMpos[X_AXIS] << ", originX=" << config->_workArea->_originX << ", pulloffX=" << xPul
                     << ") => X=" << tool1x << "; (limitY=" << _yLimitMpos[Y_AXIS] << ", originY=" << config->_workArea->_originY << ", pulloffY=" << yPul
                     << ") => Y=" << tool1y);
        } else {
            // Fallback to start position if no work area defined
            tool1x = _startMpos[X_AXIS];
            tool1y = _startMpos[Y_AXIS];
        }

        // If user already provided a Z (via subsequent M155 Z<val>) keep it, else 0
        float tool1z = _pendingZ;
        auto& cfg = WebUI::ToolConfig::getInstance();
        cfg.ensureLoaded();

        // Optional rounding of X/Y for storage consistency
    float storeTool1X = tool1x;
    float storeTool1Y = tool1y;

        WebUI::Tool t1; t1.number = 1; t1.x = storeTool1X; t1.y = storeTool1Y; t1.z = (tool1z != 0.0f ? tool1z : TOOL_BANK_Z_DEFAULT); t1.occupied = false;
        if (!cfg.updateTool(t1)) cfg.addTool(t1);
        log_info("ToolCalibration: saved tool1 pos " << t1.x << "," << t1.y << " Z=" << t1.z);

    // Populate remaining tools (2..TOOL_BANK_COUNT) using spacing along negative Y from tool1 baseline
        for (int n = 2; n <= TOOL_BANK_COUNT; ++n) {
            WebUI::Tool tn; tn.number = n; tn.x = storeTool1X; tn.y = storeTool1Y - (n - 1) * TOOL_BANK_SPACING; tn.z = TOOL_BANK_Z_DEFAULT; tn.occupied = false;
            if (!cfg.updateTool(tn)) cfg.addTool(tn);
            log_info("ToolCalibration: saved tool" << n << " pos " << tn.x << "," << tn.y << " Z=" << tn.z);
        }
    cfg.sortByNumber();
        cfg.saveConfig();

    _isCalibrating = false;
    _stage         = 0;
    sys.step_control.executeSysMotion = false;
    set_state(State::Idle);
    moveToOriginAfterCalibration();
    }

    void onLimit(Machine::LimitPin* limit) {
        if (!_isCalibrating) return;
        // Only act on the expected axis for the current stage to avoid switch bounce
        // wiping the queued move for the next axis.
        if (_stage == 1) {
            if (limit->_axis != X_AXIS) {
                return; // Ignore other axis or bounce from Y
            }
            Stepper::reset();
            plan_reset();
            float* mpos = get_mpos();
            for (size_t i = 0; i < config->_axes->_numberAxis; ++i) _xLimitMpos[i] = mpos[i];
            log_info("ToolCalibration: X +limit captured @ " << _xLimitMpos[X_AXIS]);
            _stage = 2;
            planAxisTowardPositiveLimit(Y_AXIS); // Queue Y approach
            return;
        } else if (_stage == 2) {
            if (limit->_axis != Y_AXIS) {
                return; // Ignore bounce from X while moving Y
            }
            Stepper::reset();
            plan_reset();
            float* mpos = get_mpos();
            for (size_t i = 0; i < config->_axes->_numberAxis; ++i) _yLimitMpos[i] = mpos[i];
            log_info("ToolCalibration: Y +limit captured @ " << _yLimitMpos[Y_AXIS]);
            finishCalibration();
            return;
        }
    }

    // External API to set Z after calibration (M155 Z...) when not calibrating
    void setToolZ(float z) {
        _pendingZ = z;
        // Update tool config immediately if calibration already finished
        if (!_isCalibrating) {
            auto& cfg = WebUI::ToolConfig::getInstance();
            cfg.ensureLoaded();
            WebUI::Tool* tool = cfg.getTool(1);
            if (tool) { tool->z = z; cfg.saveConfig(); log_info("ToolCalibration: Updated tool1 Z=" << z); }
        }
    }


    // (handleCycleStop removed as no longer used)
} // namespace ToolCalibration
