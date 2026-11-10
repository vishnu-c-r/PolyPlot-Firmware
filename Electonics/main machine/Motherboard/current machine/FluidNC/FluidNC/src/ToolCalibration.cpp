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
#include "Stepper.h"
#include <cmath>

namespace ToolCalibration {
    static volatile bool _isCalibrating = false;
    static int   _stage         = 0;        // 0 = none, 1 = X moving, 2 = Y moving
    static float _startMpos[MAX_N_AXIS];
    static float _xLimitMpos[MAX_N_AXIS];
    static float _yLimitMpos[MAX_N_AXIS];
    static float _pendingZ      = 0.0f;     // user-specified Z capture
    static uint32_t _seekStartMs = 0;       // millis() when current axis seek began
    static constexpr uint32_t SEEK_TIMEOUT_MS = 60000; // 60s timeout per axis seek

    static const int REPORT_LINE_NUMBER = 0;

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
        pl.motion.rapidMotion    = 1;
        pl.motion.noFeedOverride = 0;
        pl.feed_rate             = 6000.0f;
        pl.line_number           = REPORT_LINE_NUMBER;
        pl.limits_checked        = true;

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

        float travel_distance = config->_axes->_axis[axis]
            ? config->_axes->_axis[axis]->_maxTravel * 1.2f
            : 1000.0f;
        target[axis] += travel_distance;
        log_info("ToolCalibration: Planning +limit move for axis " << axis
                 << " from " << current_mpos[axis] << " to " << target[axis]);

        plan_line_data_t plan_data = {};
        plan_data.motion                = {};
        plan_data.motion.systemMotion   = 1;
        plan_data.motion.noFeedOverride = 1;
        plan_data.coolant.Mist          = 0;
        plan_data.coolant.Flood         = 0;
        plan_data.line_number           = REPORT_LINE_NUMBER;
        plan_data.is_jog                = false;
        plan_data.feed_rate             = 2000.0f;
        plan_data.limits_checked        = true;

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
        for (size_t i = 0; i < config->_axes->_numberAxis; ++i)
            _startMpos[i] = mpos[i];

        if (config->_axes->_numberAxis > 0) config->_axes->_axis[X_AXIS]->_softLimits = false;
        if (config->_axes->_numberAxis > 1) config->_axes->_axis[Y_AXIS]->_softLimits = false;

        _isCalibrating = true;
        _stage = 1;
        _seekStartMs = uint32_t(millis());
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
        // Re-enable soft limits
        if (config->_axes->_numberAxis > 0) config->_axes->_axis[X_AXIS]->_softLimits = true;
        if (config->_axes->_numberAxis > 1) config->_axes->_axis[Y_AXIS]->_softLimits = true;

        // Sanity check: captured limit positions should differ meaningfully from start
        constexpr float MIN_TRAVEL = 10.0f; // mm
        float xTravel = fabsf(_xLimitMpos[X_AXIS] - _startMpos[X_AXIS]);
        float yTravel = fabsf(_yLimitMpos[Y_AXIS] - _startMpos[Y_AXIS]);
        if (xTravel < MIN_TRAVEL || yTravel < MIN_TRAVEL) {
            log_error("ToolCalibration: Suspicious limit positions (X travel=" << xTravel
                      << "mm, Y travel=" << yTravel << "mm). Check limit switches. Aborting.");
            _isCalibrating = false;
            _stage = 0;
            set_state(State::Idle);
            return;
        }

        // Compute tool 1 position relative to work area origin
        float tool1x, tool1y;
        if (config->_workArea) {
            float xPul = getAxisPulloff(X_AXIS);
            float yPul = getAxisPulloff(Y_AXIS);

            float diffX = _xLimitMpos[X_AXIS] - config->_workArea->_originX - xPul;
            tool1x      = -diffX;

            float diffY = _yLimitMpos[Y_AXIS] - config->_workArea->_originY - yPul;
            tool1y      = -diffY;

            log_info("ToolCalibration: mapped tool1 (limitX=" << _xLimitMpos[X_AXIS]
                     << ", originX=" << config->_workArea->_originX << ", pulloffX=" << xPul
                     << ") => X=" << tool1x << "; (limitY=" << _yLimitMpos[Y_AXIS]
                     << ", originY=" << config->_workArea->_originY << ", pulloffY=" << yPul
                     << ") => Y=" << tool1y);
        } else {
            tool1x = _startMpos[X_AXIS];
            tool1y = _startMpos[Y_AXIS];
        }

        float tool1z = _pendingZ;
        auto& cfg = WebUI::ToolConfig::getInstance();
        cfg.ensureLoaded();

        int   toolCount   = config->_toolBank->_count;
        float toolSpacing = config->_toolBank->_spacing;
        float toolZDef    = config->_toolBank->_zDefault;

        // Save tool 1
        WebUI::Tool t1;
        t1.number   = 1;
        t1.x        = tool1x;
        t1.y        = tool1y;
        t1.z        = (tool1z != 0.0f ? tool1z : toolZDef);
        t1.occupied = false;
        if (!cfg.updateTool(t1)) cfg.addTool(t1);
        log_info("ToolCalibration: saved tool1 pos " << t1.x << "," << t1.y << " Z=" << t1.z);

        // Populate remaining tools using spacing along negative Y from tool 1
        for (int n = 2; n <= toolCount; ++n) {
            WebUI::Tool tn;
            tn.number   = n;
            tn.x        = tool1x;
            tn.y        = tool1y - (n - 1) * toolSpacing;
            tn.z        = toolZDef;
            tn.occupied = false;
            if (!cfg.updateTool(tn)) cfg.addTool(tn);
            log_info("ToolCalibration: saved tool" << n << " pos " << tn.x << "," << tn.y << " Z=" << tn.z);
        }

        cfg.sortByNumber();
        cfg.saveConfig();

        _isCalibrating = false;
        _stage         = 0;
        sys.step_control.executeSysMotion = false;
        set_state(State::Idle);

        if (Machine::Axes::homingMask) {
            log_info("ToolCalibration: Homing machine after calibration...");
            Machine::Homing::run_cycles(Machine::Axes::homingMask);
        }
    }

    void checkTimeout() {
        if (!_isCalibrating || _stage == 0) return;
        if ((uint32_t(millis()) - _seekStartMs) >= SEEK_TIMEOUT_MS) {
            log_error("ToolCalibration: Axis seek timed out after " << (SEEK_TIMEOUT_MS / 1000) << "s — aborting. Check limit switches.");
            Stepper::reset();
            plan_reset();
            abortCalibration();
        }
    }

    void onLimit(Machine::LimitPin* limit) {
        if (!_isCalibrating) return;

        if (_stage == 1) {
            if (limit->_axis != X_AXIS) return;

            Stepper::reset();
            plan_reset();
            float* mpos = get_mpos();
            for (size_t i = 0; i < config->_axes->_numberAxis; ++i)
                _xLimitMpos[i] = mpos[i];
            log_info("ToolCalibration: X +limit captured @ " << _xLimitMpos[X_AXIS]);

            _stage = 2;
            _seekStartMs = uint32_t(millis());
            planAxisTowardPositiveLimit(Y_AXIS);
        } else if (_stage == 2) {
            if (limit->_axis != Y_AXIS) return;

            Stepper::reset();
            plan_reset();
            float* mpos = get_mpos();
            for (size_t i = 0; i < config->_axes->_numberAxis; ++i)
                _yLimitMpos[i] = mpos[i];
            log_info("ToolCalibration: Y +limit captured @ " << _yLimitMpos[Y_AXIS]);

            finishCalibration();
        }
    }

    void setToolZ(float z) {
        _pendingZ = z;
        if (!_isCalibrating) {
            auto& cfg = WebUI::ToolConfig::getInstance();
            cfg.ensureLoaded();
            WebUI::Tool* tool = cfg.getTool(1);
            if (tool) {
                tool->z = z;
                cfg.saveConfig();
                log_info("ToolCalibration: Updated tool1 Z=" << z);
            }
        }
    }

} // namespace ToolCalibration
