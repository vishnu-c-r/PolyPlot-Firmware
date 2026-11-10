#include "WorkAreaCalibration.h"

#include "Planner.h"
#include "Protocol.h"
#include "System.h"
#include "Logging.h"
#include "MotionControl.h"
#include "Machine/MachineConfig.h"
#include "FluidError.hpp"
#include "FileStream.h"
#include "SettingsDefinitions.h" // config_filename
#include "WebUI/Commands.h"
#include "GCode.h"  // gc_sync_position
#include "Machine/Homing.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <cstdio>
#include <filesystem>

#include "WebUI/JSONEncoder.h"

using namespace Machine;

// ============================================================================
// WorkAreaCalibration — Two-Pass Workspace Measurement
//
// Measures the machine's usable workspace by seeking toward the homing-direction
// limit switches from two different starting positions.
//
// How it works:
//   1. User homes the machine (mpos = 0 at pulloff distance from limit).
//   2. User physically pushes the toolhead by hand to the FAR corner of the
//      workspace (away from limits). Steppers are idle so the gantry moves freely,
//      but firmware mpos is now stale (still shows homing position).
//   3. User runs M156 P1. Firmware resets mpos to 0 at the current physical
//      position, then seeks toward the positive limit. The captured limit_mpos
//      is the distance from the far corner to the limit switch. This is a LARGE
//      positive value (e.g. ~450mm for X).
//   4. User homes again, pushes the toolhead by hand to the NEAR corner
//      (close to origin / limit switches).
//   5. User runs M156 P2. Same reset-and-seek. The captured limit_mpos is
//      the distance from the near corner to the limit switch. This is a SMALL
//      positive value (e.g. ~30mm for X).
//   6. User runs M156 P3. Firmware computes the work area from both passes:
//        - nearDist (short pass) defines the max boundary (near the limits)
//        - farDist (long pass) defines the min boundary (far from limits)
//        - The difference (nearDist - farDist) gives the workspace span
//      Writes results to config.yaml and restarts the MCU.
//
// Coordinate system produced:
//   The output uses a coordinate system where the near-limit position is close
//   to zero. For positive-homing machines working in negative space:
//     max ≈ nearDist - pulloff  (small positive, near limits)
//     min ≈ nearDist - farDist + pulloff  (large negative, far from limits)
//     origin ≈ max + small inset  (just inside the max boundary)
// ============================================================================

namespace {
    enum class Stage { Idle, XSeek, YSeek, Done };

    // Each pass captures the limit_mpos for X and Y (distance from reset point to limit)
    struct PassData {
        bool  captured   = false;
        float limit_mpos = 0.0f;
    };

    Stage    stage       = Stage::Idle;
    int      currentPass = 0; // 1 or 2
    PassData pass1x, pass1y, pass2x, pass2y;
    uint32_t seekStartMs = 0;

    constexpr uint32_t SEEK_TIMEOUT_MS = 60000; // 60s timeout per axis seek
    constexpr float    ORIGIN_INSET    = 0.1f;  // mm inward from near-limit boundary
    const char*        tempCalibFile   = "/spiffs/calib_temp.json";

    inline float round01(float v) {
        return floorf(v * 10.0f + 0.5f) / 10.0f;
    }

    // --- JSON helpers for persisting pass data between passes ---

    float parseJsonFloat(const std::string& json, const std::string& key) {
        std::string qKey = "\"" + key + "\":";
        size_t pos = json.find(qKey);
        if (pos == std::string::npos) return 0.0f;
        pos += qKey.length();
        if (pos < json.length() && json[pos] == '"') pos++;
        return std::strtof(json.substr(pos).c_str(), nullptr);
    }

    bool parseJsonBool(const std::string& json, const std::string& key) {
        std::string qKey = "\"" + key + "\":";
        size_t pos = json.find(qKey);
        if (pos == std::string::npos) return false;
        pos += qKey.length();
        if (pos < json.length() && json[pos] == '"') pos++;
        return json.substr(pos, 4) == "true";
    }

    void saveCalibrationState() {
        std::string output;
        WebUI::JSONencoder j(&output);
        j.begin();
        j.begin_member_object("pass1");
        j.member("captured", (pass1x.captured && pass1y.captured) ? "true" : "false");
        j.fmember("limit_x", pass1x.limit_mpos);
        j.fmember("limit_y", pass1y.limit_mpos);
        j.end_object();
        j.begin_member_object("pass2");
        j.member("captured", (pass2x.captured && pass2y.captured) ? "true" : "false");
        j.fmember("limit_x", pass2x.limit_mpos);
        j.fmember("limit_y", pass2y.limit_mpos);
        j.end_object();
        j.end();

        FileStream file(tempCalibFile, "w", "");
        if (file) {
            file.write((const uint8_t*)output.c_str(), output.length());
        }
    }

    void loadCalibrationState() {
        FileStream file(tempCalibFile, "r", "");
        if (!file) return;
        std::string json;
        char buf[128];
        size_t len;
        while ((len = file.read(buf, sizeof(buf))) > 0) {
            json.append(buf, len);
        }

        size_t p1 = json.find("\"pass1\"");
        if (p1 != std::string::npos) {
            std::string sub = json.substr(p1);
            pass1x.limit_mpos = parseJsonFloat(sub, "limit_x");
            pass1y.limit_mpos = parseJsonFloat(sub, "limit_y");
            bool cap = parseJsonBool(sub, "captured");
            pass1x.captured = cap;
            pass1y.captured = cap;
        }
        size_t p2 = json.find("\"pass2\"");
        if (p2 != std::string::npos) {
            std::string sub = json.substr(p2);
            pass2x.limit_mpos = parseJsonFloat(sub, "limit_x");
            pass2y.limit_mpos = parseJsonFloat(sub, "limit_y");
            bool cap = parseJsonBool(sub, "captured");
            pass2x.captured = cap;
            pass2y.captured = cap;
        }
    }

    // Plan a seek move along one axis toward the homing-direction limit.
    // After mpos reset, this moves from 0 toward +maxTravel*1.2 (or negative
    // for negative-homing axes). The limit switch stops motion and onLimit()
    // captures the final mpos as the measured distance.
    void planAxisSeek(size_t axis) {
        float  target[MAX_N_AXIS];
        float* current_mpos = get_mpos();
        copyAxes(target, current_mpos);

        auto ax         = config->_axes->_axis[axis];
        bool toPositive = ax->_homing ? ax->_homing->_positiveDirection : true;
        float travel    = (ax->_maxTravel * 1.2f) * (toPositive ? +1.0f : -1.0f);
        target[axis]    = current_mpos[axis] + travel;

        log_info("WorkAreaCalibration: seeking axis " << axis
                 << " from " << current_mpos[axis] << " toward " << target[axis]
                 << " dir=" << (toPositive ? "+" : "-"));

        plan_line_data_t plan_data    = {};
        plan_data.motion.systemMotion   = 1;
        plan_data.motion.noFeedOverride = 1;
        plan_data.is_jog                = false;
        plan_data.feed_rate             = 2000.0f;
        plan_data.limits_checked        = true; // skip soft limits during calibration

        set_state(State::WorkAreaCalibration);
        seekStartMs = uint32_t(millis());
        if (config->_kinematics->cartesian_to_motors(target, &plan_data, current_mpos)) {
            sys.step_control.executeSysMotion = true;
            protocol_send_event(&cycleStartEvent);
        } else {
            log_error("WorkAreaCalibration: Failed to plan seek on axis " << axis);
        }
    }

    void nextStage() {
        if (stage == Stage::Idle) {
            stage = Stage::XSeek;
            planAxisSeek(X_AXIS);
        } else if (stage == Stage::XSeek) {
            stage = Stage::YSeek;
            planAxisSeek(Y_AXIS);
        } else if (stage == Stage::YSeek) {
            stage = Stage::Done;
            sys.step_control.executeSysMotion = false;
        }
    }

    void reset() {
        stage       = Stage::Idle;
        currentPass = 0;
        pass1x = PassData{};
        pass1y = PassData{};
        pass2x = PassData{};
        pass2y = PassData{};
    }

    // Compute work area boundaries for one axis from two-pass measurements.
    //
    // Both passes seek toward the SAME limit from different starting positions.
    // After mpos reset to 0, limit_mpos = distance from start to limit (positive
    // for positive-homing, negative for negative-homing).
    //
    // The SHORT pass (user near limits) gives nearDist — defines the max boundary.
    // The LONG pass (user at far corner) gives farDist — defines the min boundary.
    //
    // Example for X axis (positive homing, pulloff=2):
    //   Pass 1 (far corner):  L1 = 450  (long travel to limit)
    //   Pass 2 (near origin): L2 = 30   (short travel to limit)
    //   nearDist = 30, farDist = 450
    //   max = 30 - 2 = 28       (near-limit soft boundary)
    //   min = 30 - 450 + 2 = -418  (far-side soft boundary)
    //   Workspace span = 28 - (-418) = 446mm
    struct AxisResult {
        float pulloff;
        bool  homingPositive;
        float nearDist;    // short pass measurement
        float farDist;     // long pass measurement
        float minRaw, maxRaw, originRaw;
        float min, max, origin; // rounded to 0.1mm
    };

    AxisResult computeAxis(const PassData& p1, const PassData& p2, Machine::Axis* ax) {
        AxisResult r{};
        const float L1 = p1.limit_mpos;
        const float L2 = p2.limit_mpos;
        r.pulloff        = ax->commonPulloff();
        r.homingPositive = ax->_homing ? ax->_homing->_positiveDirection : true;

        // Auto-detect direction from measurements
        if (L1 > 0 && L2 > 0) {
            r.homingPositive = true;
        } else if (L1 < 0 && L2 < 0) {
            r.homingPositive = false;
        }

        // Separate into near (short) and far (long) distances
        if (r.homingPositive) {
            r.nearDist = std::min(L1, L2); // smaller value = closer to limit
            r.farDist  = std::max(L1, L2); // larger value = farther from limit
        } else {
            r.nearDist = std::max(L1, L2); // closer to zero = closer to limit
            r.farDist  = std::min(L1, L2); // more negative = farther from limit
        }

        // Compute soft-limit boundaries
        if (r.homingPositive) {
            // Limit is in the + direction at nearDist from the near-start position.
            // Max boundary = limit position minus pulloff (stay clear of switch)
            r.maxRaw = r.nearDist - r.pulloff;
            // Min boundary = far side of workspace. The far pass traveled farDist to
            // reach the same limit, so the far start is (farDist - nearDist) behind
            // the near start. Add pulloff to stay clear of the far physical edge.
            r.minRaw = r.nearDist - r.farDist + r.pulloff;
        } else {
            // Mirror for negative homing direction
            r.minRaw = r.nearDist + r.pulloff;
            r.maxRaw = r.nearDist - r.farDist - r.pulloff;
        }

        // Place origin just inside the boundary closest to zero (the near-limit side).
        // This is where the toolhead returns after homing.
        float nearZeroBound = (fabsf(r.minRaw) <= fabsf(r.maxRaw)) ? r.minRaw : r.maxRaw;
        if (nearZeroBound >= 0) {
            r.originRaw = nearZeroBound + ORIGIN_INSET;
        } else {
            r.originRaw = nearZeroBound - ORIGIN_INSET;
        }

        r.min    = round01(r.minRaw);
        r.max    = round01(r.maxRaw);
        r.origin = round01(r.originRaw);
        return r;
    }

    void commitConfigAndRestart() {
        // Validate that both passes completed
        if (!pass1x.captured || !pass1y.captured) {
            log_error("WorkAreaCalibration: Pass 1 incomplete — run M156 P1 first");
            return;
        }
        if (!pass2x.captured || !pass2y.captured) {
            log_error("WorkAreaCalibration: Pass 2 incomplete — run M156 P2 first");
            return;
        }

        auto axX = config->_axes->_axis[X_AXIS];
        auto axY = config->_axes->_axis[Y_AXIS];

        AxisResult xr = computeAxis(pass1x, pass2x, axX);
        AxisResult yr = computeAxis(pass1y, pass2y, axY);

        log_info("WorkAreaCalibration: X: L1=" << pass1x.limit_mpos << " L2=" << pass2x.limit_mpos
                 << " near=" << xr.nearDist << " far=" << xr.farDist
                 << " pulloff=" << xr.pulloff << " dir=" << (xr.homingPositive ? "+" : "-"));
        log_info("WorkAreaCalibration: X result: min=" << xr.min << " max=" << xr.max << " origin=" << xr.origin);

        log_info("WorkAreaCalibration: Y: L1=" << pass1y.limit_mpos << " L2=" << pass2y.limit_mpos
                 << " near=" << yr.nearDist << " far=" << yr.farDist
                 << " pulloff=" << yr.pulloff << " dir=" << (yr.homingPositive ? "+" : "-"));
        log_info("WorkAreaCalibration: Y result: min=" << yr.min << " max=" << yr.max << " origin=" << yr.origin);

        // Sanity check: work area should be at least 50mm in each direction
        constexpr float MIN_WORK_AREA = 50.0f;
        float xSpan = xr.max - xr.min;
        float ySpan = yr.max - yr.min;
        if (xSpan < MIN_WORK_AREA || ySpan < MIN_WORK_AREA) {
            log_error("WorkAreaCalibration: Work area too small (X=" << xSpan
                      << "mm, Y=" << ySpan << "mm). Check limit switches or pass positions.");
            return;
        }

        // --- Write results to config.yaml ---
        std::string cfgPath = config_filename->get();
        if (cfgPath.empty()) {
            log_error("No config filename set; cannot persist work_area");
            return;
        }

        FileStream inFile(cfgPath.c_str(), "r", "");
        if (!inFile) {
            log_error("Failed opening config for read: " << cfgPath);
            return;
        }
        size_t size = inFile.size();
        std::string yaml(size, '\0');
        inFile.read(yaml.data(), size);

        // Ensure work_area section exists
        auto ensureWorkArea = [&]() {
            size_t wa = yaml.find("work_area:");
            if (wa == std::string::npos) {
                yaml += "\nwork_area:\n  enabled: true\n  move_to_origin: true\n";
                wa = yaml.find("work_area:");
            }
            return wa;
        };
        size_t waPos = ensureWorkArea();

        // Remove duplicate work_area blocks
        auto eraseBlockAt = [&](size_t startPos) {
            size_t waLineEnd2 = yaml.find('\n', startPos);
            if (waLineEnd2 == std::string::npos) waLineEnd2 = yaml.size();
            size_t blkEnd = waLineEnd2 + 1;
            while (blkEnd < yaml.size()) {
                size_t ls = blkEnd;
                size_t nl = yaml.find('\n', ls);
                size_t le = (nl == std::string::npos) ? yaml.size() : nl + 1;
                size_t i  = ls;
                while (i < le && (yaml[i] == ' ' || yaml[i] == '\t')) i++;
                if (i == ls || ls >= yaml.size()) break;
                blkEnd = le;
                if (nl == std::string::npos) break;
            }
            yaml.erase(startPos, blkEnd - startPos);
        };

        size_t searchPos = waPos + 1;
        while (true) {
            size_t dup = yaml.find("work_area:", searchPos);
            if (dup == std::string::npos) break;
            eraseBlockAt(dup);
            searchPos = dup;
        }

        // Find the work_area block boundaries
        size_t waLineEnd = yaml.find('\n', waPos);
        if (waLineEnd == std::string::npos) waLineEnd = yaml.size();
        size_t blockStart = waLineEnd + 1;
        size_t blockEnd   = blockStart;
        while (blockEnd < yaml.size()) {
            size_t lineStart = blockEnd;
            size_t nl        = yaml.find('\n', lineStart);
            size_t lineEnd   = (nl == std::string::npos) ? yaml.size() : nl + 1;
            size_t i = lineStart;
            while (i < lineEnd && (yaml[i] == ' ' || yaml[i] == '\t')) i++;
            if (i == lineStart || lineStart >= yaml.size()) break; // column 0 = next section
            blockEnd = lineEnd;
            if (nl == std::string::npos) break;
        }

        // Detect indent used in the block
        auto detectIndent = [&]() -> std::string {
            size_t p = blockStart;
            while (p < blockEnd) {
                size_t ls = p;
                size_t nl = yaml.find('\n', ls);
                size_t le = (nl == std::string::npos) ? yaml.size() : nl + 1;
                size_t i  = ls;
                while (i < le && (yaml[i] == ' ' || yaml[i] == '\t')) i++;
                if (i > ls) return yaml.substr(ls, i - ls);
                p = le;
                if (nl == std::string::npos) break;
            }
            return std::string("  ");
        };
        const std::string indent = detectIndent();

        // Replace or insert a key: value line within the block
        auto replaceKeyInBlock = [&](const char* key, float val) {
            size_t p = blockStart;
            while (p < blockEnd) {
                size_t ls = p;
                size_t nl = yaml.find('\n', ls);
                size_t le = (nl == std::string::npos) ? yaml.size() : nl + 1;
                size_t i = ls;
                while (i < le && (yaml[i] == ' ' || yaml[i] == '\t')) i++;
                const size_t keyLen = ::strlen(key);
                if (i + keyLen + 1 <= le && yaml.compare(i, keyLen, key) == 0 && yaml[i + keyLen] == ':') {
                    char line[96];
                    snprintf(line, sizeof(line), "%s%s: %.1f\n", indent.c_str(), key, val);
                    yaml.replace(ls, le - ls, line);
                    blockEnd += ::strlen(line) - (le - ls);
                    return;
                }
                p = le;
                if (nl == std::string::npos) break;
            }
            char line[96];
            snprintf(line, sizeof(line), "%s%s: %.1f\n", indent.c_str(), key, val);
            yaml.insert(blockStart, line);
            blockEnd += ::strlen(line);
        };

        replaceKeyInBlock("min_x", xr.min);
        replaceKeyInBlock("max_x", xr.max);
        replaceKeyInBlock("origin_x", xr.origin);
        replaceKeyInBlock("min_y", yr.min);
        replaceKeyInBlock("max_y", yr.max);
        replaceKeyInBlock("origin_y", yr.origin);

        // Update in-memory config
        if (config->_workArea) {
            config->_workArea->_minX    = xr.min;
            config->_workArea->_maxX    = xr.max;
            config->_workArea->_originX = xr.origin;
            config->_workArea->_minY    = yr.min;
            config->_workArea->_maxY    = yr.max;
            config->_workArea->_originY = yr.origin;
        }

        // Atomic write: temp file then rename
        {
            std::string tmpPath = cfgPath + ".tmp";
            {
                FileStream outFile(tmpPath.c_str(), "w", "");
                if (!outFile) {
                    log_error("Failed opening temp config for write: " << tmpPath);
                    return;
                }
                auto n = outFile.write((const uint8_t*)yaml.data(), yaml.size());
                if (n != yaml.size()) {
                    log_error("Short write to temp config: wrote " << n << " of " << yaml.size());
                    return;
                }
            }
            std::error_code ec;
            std::filesystem::rename(tmpPath, cfgPath, ec);
            if (ec) {
                log_error("Failed to rename temp config: " << ec.message().c_str());
                return;
            }
        }

        log_info("WorkAreaCalibration: updated work_area in " << cfgPath << "; restarting MCU");
        WebUI::COMMANDS::restart_MCU();
    }
}

namespace WorkAreaCalibration {
    void startPass(int pass) {
        if (pass == 3) {
            loadCalibrationState();
            commitConfigAndRestart();
            return;
        }

        if (isCalibrating()) return;

        if (pass == 1) {
            reset();
        } else if (pass == 2) {
            loadCalibrationState(); // restore pass 1 data
        }
        currentPass = pass;

        set_state(State::WorkAreaCalibration);

        // Disable soft limits while seeking
        if (config->_axes->_numberAxis > 0) config->_axes->_axis[X_AXIS]->_softLimits = false;
        if (config->_axes->_numberAxis > 1) config->_axes->_axis[Y_AXIS]->_softLimits = false;

        // Reset X/Y mpos to 0 at the current physical position (user pushed toolhead here).
        // This makes limit_mpos = distance from this position to the limit switch.
        {
            float* cur = get_mpos();
            float  newMP[MAX_N_AXIS];
            copyAxes(newMP, cur);
            if (config->_axes->_numberAxis > 0) newMP[X_AXIS] = 0.0f;
            if (config->_axes->_numberAxis > 1) newMP[Y_AXIS] = 0.0f;
            set_motor_steps_from_mpos(newMP);
            gc_sync_position();
            plan_sync_position();
            log_info("WorkAreaCalibration: mpos reset to 0 for pass " << currentPass);
        }

        nextStage();
    }

    bool isCalibrating() {
        return stage != Stage::Idle && stage != Stage::Done;
    }

    void onLimit(LimitPin* limit) {
        if (!isCalibrating()) return;
        size_t axis = (size_t)limit->_axis;

        // Only act on the axis we're currently seeking
        if ((stage == Stage::XSeek && axis != X_AXIS) ||
            (stage == Stage::YSeek && axis != Y_AXIS)) {
            return;
        }

        float* mpos = get_mpos();
        float  m    = mpos[axis];
        log_info("WorkAreaCalibration: axis " << axis << " limit at mpos=" << m
                 << " (pass " << currentPass << ")");

        // Sanity check: should have traveled a meaningful distance from reset point (0)
        constexpr float MIN_TRAVEL = 10.0f;
        if (fabsf(m) < MIN_TRAVEL) {
            log_warn("WorkAreaCalibration: Only " << fabsf(m)
                     << "mm travel on axis " << axis << " — check switch or starting position");
        }

        // Store the measurement
        if (currentPass == 1) {
            if (axis == X_AXIS && !pass1x.captured) { pass1x.captured = true; pass1x.limit_mpos = m; }
            if (axis == Y_AXIS && !pass1y.captured) { pass1y.captured = true; pass1y.limit_mpos = m; }
        } else if (currentPass == 2) {
            if (axis == X_AXIS && !pass2x.captured) { pass2x.captured = true; pass2x.limit_mpos = m; }
            if (axis == Y_AXIS && !pass2y.captured) { pass2y.captured = true; pass2y.limit_mpos = m; }
        }

        Stepper::reset();
        plan_reset();
        nextStage();

        if (stage == Stage::Done) {
            // Re-enable soft limits
            if (config->_axes->_numberAxis > 0) config->_axes->_axis[X_AXIS]->_softLimits = true;
            if (config->_axes->_numberAxis > 1) config->_axes->_axis[Y_AXIS]->_softLimits = true;

            saveCalibrationState();
            set_state(State::Idle);
            stage = Stage::Idle;
        }
    }

    void checkTimeout() {
        if (!isCalibrating()) return;
        if ((uint32_t(millis()) - seekStartMs) >= SEEK_TIMEOUT_MS) {
            log_error("WorkAreaCalibration: Seek timed out after " << (SEEK_TIMEOUT_MS / 1000)
                      << "s — aborting. Check limit switches.");
            Stepper::reset();
            plan_reset();
            abortCalibration();
        }
    }

    void abortCalibration() {
        reset();
        if (config->_axes->_numberAxis > 0) config->_axes->_axis[X_AXIS]->_softLimits = true;
        if (config->_axes->_numberAxis > 1) config->_axes->_axis[Y_AXIS]->_softLimits = true;
        set_state(State::Idle);
    }
}
