#include "WorkAreaCalibration.h"
#include "System.h"
#include "Protocol.h"
#include "Planner.h"
#include "Limits.h"
#include "Report.h"
#include "Machine/MachineConfig.h"
#include "FileStream.h"
#include "Driver/localfs.h"
#include "FluidPath.h"
#include "WebUI/Commands.h"
#include "SettingsDefinitions.h"
#include <cstring>
#include <cstdio>
#include <cmath>

namespace WorkAreaCalibration {
    enum Stage { None, XSeek, YSeek, Compute };
    static bool  running = false;
    static Stage stage   = None;
    static int   calibration_mode = 0;  // legacy auto modes (kept for internal use if needed)
    // Mode flags for interactive flows
    static bool  modeAutoFull    = false;
    static bool  modeCaptureMax  = false;
    static bool  modeCaptureMin  = false;
    static float startMpos[MAX_N_AXIS];
    static float xLimitMpos[MAX_N_AXIS];
    static float yLimitMpos[MAX_N_AXIS];
    // Track interactive results
    static bool  haveMax = false;
    static bool  haveMin = false;

    static float getPulloff(size_t axis) {
        if (axis < config->_axes->_numberAxis && config->_axes->_axis[axis])
            return config->_axes->_axis[axis]->commonPulloff();
        return 0.0f;
    }

    static bool planTowardPositiveLimit(size_t axis) {
        float target[MAX_N_AXIS];
        float* cur = get_mpos();
        copyAxes(target, cur);
        float travel = config->_axes->_axis[axis] ? config->_axes->_axis[axis]->_maxTravel * 1.2f : 1000.0f;
        target[axis] += travel;
        plan_line_data_t pl = {};
        pl.motion.systemMotion   = 1;
        pl.motion.noFeedOverride = 1;
        pl.feed_rate             = 2000.0f;
        pl.limits_checked        = true; // skip soft limits
        if (config->_kinematics->cartesian_to_motors(target, &pl, cur)) {
            protocol_send_event(&cycleStartEvent);
            return true;
        }
        return false;
    }

    static bool planTowardNegativeLimit(size_t axis) {
        float target[MAX_N_AXIS];
        float* cur = get_mpos();
        copyAxes(target, cur);
        float travel = config->_axes->_axis[axis] ? config->_axes->_axis[axis]->_maxTravel * 1.2f : 1000.0f;
        target[axis] -= travel;
        plan_line_data_t pl = {};
        pl.motion.systemMotion   = 1;
        pl.motion.noFeedOverride = 1;
        pl.feed_rate             = 2000.0f;
        pl.limits_checked        = true; // skip soft limits
        if (config->_kinematics->cartesian_to_motors(target, &pl, cur)) {
            protocol_send_event(&cycleStartEvent);
            return true;
        }
        return false;
    }

    static float round3(float v) {
        // Round to 0.1mm to avoid floating artifacts like -30.19999
        return floorf(v * 10.0f + (v >= 0 ? 0.5f : -0.5f)) / 10.0f;
    }

    static void writeWorkAreaToConfig(float minx, float maxx, float miny, float maxy, float ox, float oy) {
        // Debug: Log the values being written to config
        log_info("Work Area Cal: Writing to config - Origin X: " << ox << ", Y: " << oy);
        log_info("Work Area Cal: Writing to config - X bounds: [" << minx << " to " << maxx << "], Y bounds: [" << miny << " to " << maxy << "]");
        
        // Resolve current config filename and filesystem
        const char* cfgName = config_filename->get();
        const char* fsName  = localfsName ? localfsName : defaultLocalfsName;
        std::error_code ec;
        FluidPath cfgPath { cfgName, fsName, ec };
        if (ec) {
            log_error("Cannot resolve config path: " << cfgName);
            return;
        }

        // Read existing content
        std::string content;
        try {
            FileStream in(cfgPath, "r");
            content.resize(in.size());
            if (in.size() > 0) {
                in.read(&content[0], in.size());
            }
        } catch (...) {
            log_error("Cannot open config for read: " << cfgPath.c_str());
            return;
        }

        auto replaceLine = [&](const char* key, float val) {
            char needle[64];
            snprintf(needle, sizeof(needle), "%s:", key);
            size_t pos = content.find(needle);
            if (pos == std::string::npos) return;
            size_t lineStart = content.rfind('\n', pos);
            if (lineStart == std::string::npos) lineStart = 0; else lineStart += 1;
            size_t lineEnd = content.find('\n', pos);
            if (lineEnd == std::string::npos) lineEnd = content.size();
            char repl[64];
            // one decimal
            snprintf(repl, sizeof(repl), "%s: %.1f", key, val);
            content.replace(lineStart, lineEnd - lineStart, std::string("  ") + repl);
        };

        replaceLine("min_x", minx);
        replaceLine("min_y", miny);
        replaceLine("max_x", maxx);
        replaceLine("max_y", maxy);
        replaceLine("origin_x", ox);
        replaceLine("origin_y", oy);

        // Write to temp and atomically replace
        FluidPath tmpPath { std::string(cfgPath.c_str()) + ".new", fsName, ec };
        try {
            FileStream out(tmpPath, "w");
            if (!content.empty()) {
                out.write(reinterpret_cast<const uint8_t*>(content.data()), content.size());
            }
        } catch (...) {
            log_error("Cannot open temp config for write: " << tmpPath.c_str());
            return;
        }
        stdfs::rename(tmpPath, cfgPath, ec);
        if (ec) {
            log_error("Rename to config.yaml failed");
            return;
        }
        log_info("config.yaml updated with new work area and origin");

        // Restart MCU to apply
        WebUI::COMMANDS::restart_MCU();
    }

    void saveWorkAreaToConfigAndRestart() {
        if (!(config && config->_workArea)) return;
        auto* wa = config->_workArea;
        // Round to 0.1 for consistency
        auto r = [](float v) { return floorf(v * 10.0f + (v >= 0 ? 0.5f : -0.5f)) / 10.0f; };
        writeWorkAreaToConfig(r(wa->_minX), r(wa->_maxX), r(wa->_minY), r(wa->_maxY), r(wa->_originX), r(wa->_originY));
    }

    static void computeAndPersist() {
        float xPul = getPulloff(X_AXIS);
        float yPul = getPulloff(Y_AXIS);

        // Compute base origin as the measured max positions (after pulloff) in negative-space
        float base_origin_x = round3(-(xLimitMpos[X_AXIS] - xPul));
        float base_origin_y = round3(-(yLimitMpos[Y_AXIS] - yPul));

        // Compute min bounds based on travel from origin to limits
        float min_x = round3(base_origin_x - (xLimitMpos[X_AXIS] - startMpos[X_AXIS] - xPul));
        float min_y = round3(base_origin_y - (yLimitMpos[Y_AXIS] - startMpos[Y_AXIS] - yPul));
        // Max bounds slightly inside origin to keep soft-limit guard
        float max_x = round3(base_origin_x - 0.3f);
        float max_y = round3(base_origin_y - 0.3f);

        // Apply mode-specific logic
        float final_origin_x, final_origin_y;
        float final_min_x, final_max_x, final_min_y, final_max_y;
        
        switch (calibration_mode) {
            case 1:  // M156 P1 - Maximum bounds mode
                final_origin_x = base_origin_x;
                final_origin_y = base_origin_y;
                final_min_x = min_x;
                final_max_x = max_x;
                final_min_y = min_y;
                final_max_y = max_y;
                log_info("Work Area Cal: Mode P1 - Maximum bounds");
                break;
                
            case 2:  // M156 P2 - Minimum bounds mode
                final_origin_x = min_x;
                final_origin_y = min_y;
                final_min_x = min_x;
                final_max_x = min_x;
                final_min_y = min_y;
                final_max_y = min_y;
                log_info("Work Area Cal: Mode P2 - Minimum bounds");
                break;
                
            default:  // M156 (no P) - Default mode (max - 0.02)
                final_origin_x = round3(max_x - 0.02f);
                final_origin_y = round3(max_y - 0.02f);
                final_min_x = min_x;
                final_max_x = max_x;
                final_min_y = min_y;
                final_max_y = max_y;
                log_info("Work Area Cal: Mode Default - Origin at max - 0.02");
                break;
        }

        // Debug: Log the calculated work origin coordinates
        log_info("Work Area Cal: Calculated work origin - X: " << final_origin_x << ", Y: " << final_origin_y);

        // Debug: Log the calculated work area bounds
        log_info("Work Area Cal: Calculated bounds - X: [" << final_min_x << " to " << final_max_x << "], Y: [" << final_min_y << " to " << final_max_y << "]");

        // Mark done and return to idle before restart
        running = false;
        stage   = None;
        set_state(State::Idle);
        writeWorkAreaToConfig(final_min_x, final_max_x, final_min_y, final_max_y, final_origin_x, final_origin_y);
    }

    void start(int mode) {
        if (running) return;
        calibration_mode = mode;  // Store the calibration mode
        modeAutoFull     = true;
        modeCaptureMax   = false;
        modeCaptureMin   = false;
        log_info("Work Area Cal: Starting calibration with mode " << mode);
        set_state(State::ToolCalibration); // reuse motion gating like systemMotion
        float* mpos = get_mpos();
        for (size_t i = 0; i < config->_axes->_numberAxis; ++i) startMpos[i] = mpos[i];
        // Debug: Log starting position
        log_info("Work Area Cal: Starting calibration from position - X: " << startMpos[X_AXIS] << ", Y: " << startMpos[Y_AXIS]);
        running = true; stage = XSeek;
        planTowardPositiveLimit(X_AXIS);
    }

    bool isRunning() { return running; }

    void abort() { running = false; stage = None; set_state(State::Idle); }

    void onLimit(Machine::LimitPin* limit) {
        if (!running) return;
        if (stage == XSeek && limit->_axis == X_AXIS) {
            Stepper::reset(); plan_reset();
            float* mpos = get_mpos(); copyAxes(xLimitMpos, mpos);
            // Debug: Log X limit detection
            log_info("Work Area Cal: X limit detected at machine position: " << xLimitMpos[X_AXIS]);
            stage = YSeek;
            if (modeCaptureMin) {
                planTowardNegativeLimit(Y_AXIS);
            } else {
                planTowardPositiveLimit(Y_AXIS);
            }
            return;
        }
        if (stage == YSeek && limit->_axis == Y_AXIS) {
            Stepper::reset(); plan_reset();
            float* mpos = get_mpos(); copyAxes(yLimitMpos, mpos);
            // Debug: Log Y limit detection
            log_info("Work Area Cal: Y limit detected at machine position: " << yLimitMpos[Y_AXIS]);
            if (modeCaptureMax) {
                // Update max from captured positive limits (no persist)
                if (config && config->_workArea) {
                    float xPul = getPulloff(X_AXIS);
                    float yPul = getPulloff(Y_AXIS);
                    float maxx = round3(-(xLimitMpos[X_AXIS] - xPul));
                    float maxy = round3(-(yLimitMpos[Y_AXIS] - yPul));
                    config->_workArea->_maxX = maxx;
                    config->_workArea->_maxY = maxy;
                    haveMax                   = true;
                    log_info("Work Area Cal: Captured MAX -> maxX:" << maxx << " maxY:" << maxy);
                }
                running = false; stage = None; set_state(State::Idle);
                modeCaptureMax = false;
                return;
            }
            if (modeCaptureMin) {
                // Update min from current (negative) limit position (no persist)
                if (config && config->_workArea) {
                    float* cm = get_mpos();
                    config->_workArea->_minX = round3(cm[X_AXIS]);
                    config->_workArea->_minY = round3(cm[Y_AXIS]);
                    haveMin                   = true;
                    log_info("Work Area Cal: Captured MIN -> minX:" << config->_workArea->_minX << " minY:" << config->_workArea->_minY);
                }
                running = false; stage = None; set_state(State::Idle);
                modeCaptureMin = false;
                return;
            }
            // Legacy full-auto path
            stage = Compute; computeAndPersist(); return;
        }
    }

    // --- Interactive API ---
    void startCaptureMax() {
        // Run a quick seek X+ then Y+, store as max in config (no reboot)
        if (running) return;
        haveMax = false;
        modeAutoFull   = false;
        modeCaptureMax = true;
        modeCaptureMin = false;
        set_state(State::ToolCalibration);
        float* mpos = get_mpos(); copyAxes(startMpos, mpos);
        running = true; stage = XSeek;
        planTowardPositiveLimit(X_AXIS);
    }

    void startCaptureMin() {
        // Seek X- then Y- and capture min in config (no reboot)
        if (running) return;
        haveMin = false;
        modeAutoFull   = false;
        modeCaptureMax = false;
        modeCaptureMin = true;
        set_state(State::ToolCalibration);
        float* mpos = get_mpos(); copyAxes(startMpos, mpos);
        running = true; stage = XSeek;
        planTowardNegativeLimit(X_AXIS);
    }

    void finalizeAndSave() {
        if (!(config && config->_workArea)) return;
        // If we previously ran a capture max sequence, update max from last measured limit positions
        if (modeCaptureMax || xLimitMpos[X_AXIS] != 0 || yLimitMpos[Y_AXIS] != 0) {
            float xPul = getPulloff(X_AXIS);
            float yPul = getPulloff(Y_AXIS);
            float maxx = round3(-(xLimitMpos[X_AXIS] - xPul));
            float maxy = round3(-(yLimitMpos[Y_AXIS] - yPul));
            config->_workArea->_maxX = maxx;
            config->_workArea->_maxY = maxy;
            haveMax = true;
            log_info("Work Area Cal: Updated max from capture -> maxX:" << maxx << " maxY:" << maxy);
        }
        if (!(haveMax && haveMin)) {
            log_warn("Work Area Cal: finalize called without both min and max set");
        }
        // Compute origin as max - 0.02
        auto r1 = [](float v) { return floorf(v * 10.0f + (v >= 0 ? 0.5f : -0.5f)) / 10.0f; };
        float ox = r1(config->_workArea->_maxX - 0.02f);
        float oy = r1(config->_workArea->_maxY - 0.02f);
        // Persist
        writeWorkAreaToConfig(config->_workArea->_minX, config->_workArea->_maxX,
                              config->_workArea->_minY, config->_workArea->_maxY,
                              ox, oy);
    }
}
