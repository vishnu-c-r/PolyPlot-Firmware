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
#include <tuple>
#include <cmath>
#include <string>
#include <cstdio>

#include "WebUI/JSONEncoder.h"

using namespace Machine;

namespace {
    enum class Stage { Idle, XSeek, YSeek, Done };
    struct PassData { bool captured = false; float limit_mpos = 0.0f; float start_mpos = 0.0f; };

    Stage stage = Stage::Idle;
    int currentPass = 0; // 1 or 2
    PassData pass1x, pass1y, pass2x, pass2y;
    bool softLimitsPrev = false;

    constexpr float ORIGIN_INSET = 0.1f; // mm inward from chosen bound
    const char* tempCalibFile = "/spiffs/calib_temp.json";

    inline float round01(float v) {
        return floorf(v * 10.0f + 0.5f) / 10.0f;
    }

    // Helper to parse a float from JSON string given a key
    float parseJsonFloat(const std::string& json, const std::string& key) {
        std::string qKey = "\"" + key + "\":";
        size_t pos = json.find(qKey);
        if (pos == std::string::npos) return 0.0f;
        pos += qKey.length();
        // Skip optional quote if the encoder added one
        if (pos < json.length() && json[pos] == '"') pos++;
        return std::strtof(json.substr(pos).c_str(), nullptr);
    }

    // Helper to parse bool
    bool parseJsonBool(const std::string& json, const std::string& key) {
        std::string qKey = "\"" + key + "\":";
        size_t pos = json.find(qKey);
        if (pos == std::string::npos) return false;
        pos += qKey.length();
        // Skip optional quote
        if (pos < json.length() && json[pos] == '"') pos++;
        // check for "true"
        if (json.substr(pos, 4) == "true") return true;
        return false;
    }

    void saveCalibrationState() {
        std::string output;
        WebUI::JSONencoder j(&output);
        j.begin();
        j.begin_member_object("pass1");
        j.member("captured", (pass1x.captured && pass1y.captured) ? "true" : "false");
        j.fmember("limit_x", pass1x.limit_mpos);
        j.fmember("start_x", pass1x.start_mpos);
        j.fmember("limit_y", pass1y.limit_mpos);
        j.fmember("start_y", pass1y.start_mpos);
        j.end_object();

        j.begin_member_object("pass2");
        j.member("captured", (pass2x.captured && pass2y.captured) ? "true" : "false");
        j.fmember("limit_x", pass2x.limit_mpos);
        j.fmember("start_x", pass2x.start_mpos);
        j.fmember("limit_y", pass2y.limit_mpos);
        j.fmember("start_y", pass2y.start_mpos);
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

        // Parse Pass 1
        size_t p1 = json.find("\"pass1\"");
        if (p1 != std::string::npos) {
            std::string sub = json.substr(p1);
            pass1x.limit_mpos = parseJsonFloat(sub, "limit_x");
            pass1x.start_mpos = parseJsonFloat(sub, "start_x");
            pass1y.limit_mpos = parseJsonFloat(sub, "limit_y");
            pass1y.start_mpos = parseJsonFloat(sub, "start_y");
            bool cap = parseJsonBool(sub, "captured");
            pass1x.captured = cap; pass1y.captured = cap;
        }
        // Parse Pass 2
        size_t p2 = json.find("\"pass2\"");
        if (p2 != std::string::npos) {
            std::string sub = json.substr(p2);
            pass2x.limit_mpos = parseJsonFloat(sub, "limit_x");
            pass2x.start_mpos = parseJsonFloat(sub, "start_x");
            pass2y.limit_mpos = parseJsonFloat(sub, "limit_y");
            pass2y.start_mpos = parseJsonFloat(sub, "start_y");
            bool cap = parseJsonBool(sub, "captured");
            pass2x.captured = cap; pass2y.captured = cap;
        }
    }

    // Plan a long move along a single axis toward the homing-direction limit as a system motion
    void planAxisSeek(size_t axis) {
        float target[MAX_N_AXIS];
        float* current_mpos = get_mpos();
        copyAxes(target, current_mpos);
        auto ax = config->_axes->_axis[axis];
        bool toPositive = ax->_homing ? ax->_homing->_positiveDirection : true;
        float travel    = (ax->_maxTravel * 1.2f) * (toPositive ? +1.0f : -1.0f);
        target[axis]    = current_mpos[axis] + travel;

    log_info("WorkAreaCalibration: planning seek on axis " << axis
         << " from " << current_mpos[axis] << " to " << target[axis]
         << " dir=" << (toPositive ? "+" : "-") << " feed="
         << 2000.0f);

        plan_line_data_t plan_data = {};
        plan_data.motion.systemMotion   = 1;
        plan_data.motion.noFeedOverride = 1;
    // Match ToolCalibration calibration seek style (no rapid flag, fixed feed)
        plan_data.is_jog                = false;
    plan_data.feed_rate      = 2000.0f; // same as ToolCalibration
        plan_data.limits_checked = true; // skip soft limits

        // Enter WorkAreaCalibration state before queuing the move, so cycleStart will kick system motion
        set_state(State::WorkAreaCalibration);
        if (config->_kinematics->cartesian_to_motors(target, &plan_data, current_mpos)) {
            // Mark as system motion and kick the cycle start
            sys.step_control.executeSysMotion = true;
            protocol_send_event(&cycleStartEvent); // start executing queued system motion
        } else {
            log_error("WorkAreaCalibration: Failed to plan axis seek move on axis " << axis);
        }
    }

    void nextStage() {
        if (stage == Stage::Idle) {
            stage = Stage::XSeek;
            // Capture starting X at the moment we begin the X seek
            float* mpos = get_mpos();
            if (currentPass == 1) {
                pass1x.start_mpos = mpos[X_AXIS];
            } else if (currentPass == 2) {
                pass2x.start_mpos = mpos[X_AXIS];
            }
            planAxisSeek(X_AXIS);
        } else if (stage == Stage::XSeek) {
            stage = Stage::YSeek;
            // Capture starting Y at the moment we begin the Y seek
            float* mpos = get_mpos();
            if (currentPass == 1) {
                pass1y.start_mpos = mpos[Y_AXIS];
            } else if (currentPass == 2) {
                pass2y.start_mpos = mpos[Y_AXIS];
            }
            planAxisSeek(Y_AXIS);
        } else if (stage == Stage::YSeek) {
            stage = Stage::Done;
            // Stop any further motion execution for system motion
            sys.step_control.executeSysMotion = false;
        }
    }

    void reset() {
        stage = Stage::Idle;
        currentPass = 0;
        pass1x = PassData{}; pass1y = PassData{}; pass2x = PassData{}; pass2y = PassData{};
    }

    void commitConfigAndRestart() {
        auto axX = config->_axes->_axis[X_AXIS];
        auto axY = config->_axes->_axis[Y_AXIS];

        // Compute per-axis using both passes. For each axis we seek the same limit both passes.
        struct AxisResult {
            float L;         // captured limit position (from pass1)
            float start1;
            float start2;
            float pulloff;
            bool  pos;       // homing direction positive?
            // raw (unrounded)
            float minRaw;
            float maxRaw;
            float originRaw;
            // rounded to 0.1mm
            float min;
            float max;
            float origin;
        };

        auto computeAxis = [&](const PassData& p1, const PassData& p2, Machine::Axis* axs) -> AxisResult {
            AxisResult r{};
            // Use both passes: each pass starts with mpos reset to 0, so limit_mpos is the distance from the current physical
            // starting position to the same limit direction.
            const float L1 = p1.limit_mpos;
            const float L2 = p2.limit_mpos;
            r.L       = L1;  // keep for logging
            r.start1  = p1.start_mpos;  // will be 0 by design after reset
            r.start2  = p2.start_mpos;  // will be 0 by design after reset
            r.pulloff = axs->commonPulloff();
            r.pos     = axs->_homing ? axs->_homing->_positiveDirection : true;

            // Override direction based on measured values if they are consistent
            // If we moved positive to hit limits, we are homing to Max -> Positive Homing
            if (L1 > 0 && L2 > 0) {
                r.pos = true;
            } 
            // If we moved negative to hit limits, we are homing to Min -> Negative Homing
            else if (L1 < 0 && L2 < 0) {
                r.pos = false;
            }

            float nearDist, farDist;
            if (r.pos) {
                nearDist = std::min(L1, L2); // e.g. 5
                farDist  = std::max(L1, L2); // e.g. 300
            } else {
                nearDist = std::max(L1, L2); // e.g. -5 (larger value, smaller magnitude)
                farDist  = std::min(L1, L2); // e.g. -300 (smaller value, larger magnitude)
            }

            // We define the coordinate system such that the "Start" of the Short Pass (near limit) is 0.0.
            // This corresponds to the machine position after pull-off (assuming the user started P1/P2 from there).
            
            if (r.pos) {
                // Positive Homing. Limit is at +nearDist.
                // Soft Max (near Limit) = Limit - pulloff
                r.maxRaw = nearDist - r.pulloff;
                
                // Soft Min (Far side) = Limit - farDist + pulloff
                // (Assuming Far Start was at the physical limit)
                r.minRaw = nearDist - farDist + r.pulloff;
            } else {
                // Negative Homing. Limit is at nearDist (negative).
                // Soft Min (near Limit) = Limit + pulloff
                r.minRaw = nearDist + r.pulloff;
                
                // Soft Max (Far side) = Limit - farDist - pulloff
                r.maxRaw = nearDist - farDist - r.pulloff;
            }

            // Place origin near the bound closer to zero and inset slightly inward.
            float nearZero = (fabsf(r.minRaw) <= fabsf(r.maxRaw)) ? r.minRaw : r.maxRaw;
            if (nearZero >= 0) {
                r.originRaw = nearZero + ORIGIN_INSET; // move inward from the zero side
            } else {
                r.originRaw = nearZero - ORIGIN_INSET; // move inward (more negative)
            }

            r.min    = round01(r.minRaw);
            r.max    = round01(r.maxRaw);
            r.origin = round01(r.originRaw);
            return r;
        };

        AxisResult xr = computeAxis(pass1x, pass2x, axX);
        AxisResult yr = computeAxis(pass1y, pass2y, axY);

        // Report measured inputs and both raw/rounded results per axis before writing & restart
        log_info("WorkAreaCalibration: X inputs: L=" << xr.L
                 << ", starts=[" << xr.start1 << ", " << xr.start2 << "]"
                 << ", pulloff=" << xr.pulloff
                 << ", homingDir=" << (xr.pos ? "+" : "-") );
        log_info("WorkAreaCalibration: X results: min=" << xr.min << ", max=" << xr.max << ", origin=" << xr.origin
                 << " (raw min=" << xr.minRaw << ", max=" << xr.maxRaw << ", origin=" << xr.originRaw << ")");

        log_info("WorkAreaCalibration: Y inputs: L=" << yr.L
                 << ", starts=[" << yr.start1 << ", " << yr.start2 << "]"
                 << ", pulloff=" << yr.pulloff
                 << ", homingDir=" << (yr.pos ? "+" : "-") );
        log_info("WorkAreaCalibration: Y results: min=" << yr.min << ", max=" << yr.max << ", origin=" << yr.origin
                 << " (raw min=" << yr.minRaw << ", max=" << yr.maxRaw << ", origin=" << yr.originRaw << ")");

        // Short summary line too
        log_info("WorkAreaCalibration: computed work_area summary: X[min,max,origin]="
                 << "[" << xr.min << ", " << xr.max << ", " << xr.origin
                 << "] Y[min,max,origin]="
                 << "[" << yr.min << ", " << yr.max << ", " << yr.origin << "]");

        // Write YAML atomically: read current config, replace or insert work_area fields
        std::string cfgPath = config_filename->get();
        if (cfgPath.empty()) {
            log_error("No config filename set; cannot persist work_area");
            return;
        }
        // Read YAML
        FileStream inFile(cfgPath.c_str(), "r", "");
        if (!inFile) {
            log_error("Failed opening config for read: " << cfgPath);
            return;
        }
        size_t size = inFile.size();
        std::string yaml(size, '\0');
        inFile.read(yaml.data(), size);
        // Ensure work_area section exists and keys use colon-form format
        auto ensureWorkArea = [&]() {
            size_t wa = yaml.find("work_area:");
            if (wa == std::string::npos) {
                // Append a well-formed work_area block
                std::string block;
                block += "\nwork_area:\n";
                block += "  enabled: true\n";
                block += "  move_to_origin: true\n";
                yaml += block;
                wa = yaml.find("work_area:");
            }
            return wa;
        };
        size_t waPos = ensureWorkArea();

        // Remove any duplicate work_area blocks beyond the first to avoid ambiguity
        auto eraseBlockAt = [&](size_t startPos) {
            // find end of line
            size_t waLineEnd2 = yaml.find('\n', startPos);
            if (waLineEnd2 == std::string::npos) waLineEnd2 = yaml.size();
            size_t blkStart2 = waLineEnd2 + 1;
            size_t blkEnd2   = blkStart2;
            while (blkEnd2 < yaml.size()) {
                size_t ls = blkEnd2;
                size_t nl = yaml.find('\n', ls);
                size_t le = (nl == std::string::npos) ? yaml.size() : nl + 1;
                size_t i  = ls;
                while (i < le && (yaml[i] == ' ' || yaml[i] == '\t')) i++;
                if (i == ls || ls >= yaml.size()) break; // new top-level or EOF
                blkEnd2 = le;
                if (nl == std::string::npos) break;
            }
            yaml.erase(startPos, blkEnd2 - startPos);
        };

        // Look for additional "work_area:" occurrences after the first and remove them
        size_t searchPos = waPos + 1;
        while (true) {
            size_t dup = yaml.find("work_area:", searchPos);
            if (dup == std::string::npos) break;
            eraseBlockAt(dup);
            searchPos = dup; // continue searching from here in case of adjacent blocks
        }

        // Determine the work_area block range [blockStart, blockEnd)
        size_t waLineStart = yaml.rfind('\n', waPos);
        if (waLineStart == std::string::npos) waLineStart = 0; else waLineStart += 1;
        size_t waLineEnd = yaml.find('\n', waPos);
        if (waLineEnd == std::string::npos) waLineEnd = yaml.size();
        size_t blockStart = waLineEnd + 1; // first line after 'work_area:'
        // block ends at next line that starts at column 0 (non-space) or end of file
        size_t blockEnd = blockStart;
        while (blockEnd < yaml.size()) {
            size_t lineStart = blockEnd;
            size_t nl        = yaml.find('\n', lineStart);
            size_t lineEnd   = (nl == std::string::npos) ? yaml.size() : nl + 1;
            // count leading spaces
            size_t i = lineStart;
            while (i < lineEnd && (yaml[i] == ' ' || yaml[i] == '\t')) i++;
            if (i == lineStart || lineStart >= yaml.size()) {
                // starts at column 0 => new top-level block
                break;
            }
            blockEnd = lineEnd;
            if (nl == std::string::npos) break;
        }
        // Helper to find existing indent within the block
        auto detectIndent = [&]() -> std::string {
            size_t p = blockStart;
            while (p < blockEnd) {
                size_t ls = p;
                size_t nl = yaml.find('\n', ls);
                size_t le = (nl == std::string::npos) ? yaml.size() : nl + 1;
                size_t i  = ls;
                while (i < le && (yaml[i] == ' ' || yaml[i] == '\t')) i++;
                if (i > ls) {
                    return yaml.substr(ls, i - ls);
                }
                p = le;
                if (nl == std::string::npos) break;
            }
            return std::string("  ");
        };
        const std::string indent = detectIndent();

        auto replaceKeyInBlock = [&](const char* key, float val) {
            // Search line by line for ^\s*key:
            size_t p = blockStart;
            while (p < blockEnd) {
                size_t ls = p;
                size_t nl = yaml.find('\n', ls);
                size_t le = (nl == std::string::npos) ? yaml.size() : nl + 1;
                // compute leading whitespace
                size_t i = ls;
                while (i < le && (yaml[i] == ' ' || yaml[i] == '\t')) i++;
                // match key
                const size_t keyLen = ::strlen(key);
                if (i + keyLen + 1 <= le && yaml.compare(i, keyLen, key) == 0 && (i + keyLen < le) && yaml[i + keyLen] == ':') {
                    // replace entire line
                    char line[96];
                    snprintf(line, sizeof(line), "%s%s: %.1f\n", indent.c_str(), key, val);
                    yaml.replace(ls, le - ls, line);
                    // adjust blockEnd due to size change
                    size_t delta = ::strlen(line) - (le - ls);
                    blockEnd += delta;
                    return;
                }
                p = le;
                if (nl == std::string::npos) break;
            }
            // Not found: insert at blockStart with detected indent
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
            config->_workArea->_minX = xr.min;
            config->_workArea->_maxX = xr.max;
            config->_workArea->_originX = xr.origin;
            config->_workArea->_minY = yr.min;
            config->_workArea->_maxY = yr.max;
            config->_workArea->_originY = yr.origin;
        }

        // Write back in-place to the existing config.yaml
        // Clean up any stale temp file from older firmware versions
        {
            std::string tmpPath = cfgPath + ".new";
            std::remove(tmpPath.c_str());
        }
        {
            FileStream outFile(cfgPath.c_str(), "w", "");
            if (!outFile) {
                log_error("Failed opening config for write: " << cfgPath);
                return;
            }
            auto n = outFile.write((const uint8_t*)yaml.data(), yaml.size());
            if (n != yaml.size()) {
                log_error("Short write updating config.yaml: wrote " << n << " of " << yaml.size());
                return;
            }
        }

        log_info("WorkAreaCalibration: updated work_area in-place: " << cfgPath << "; restarting MCU");

        // Request restart
        WebUI::COMMANDS::restart_MCU();
    }
}

namespace WorkAreaCalibration {
    void startPass(int pass) {
        if (pass == 3) {
            // Commit command
            loadCalibrationState();
            commitConfigAndRestart();
            return;
        }

        if (isCalibrating()) return; // don't re-enter
        
        // Only fully reset before pass 1 so we keep pass 1 measurements for pass 2
        if (pass == 1) {
            reset();
        } else if (pass == 2) {
            loadCalibrationState(); // Restore P1 if needed
        }
        currentPass = pass;
    // Enter calibration state up-front so protocol knows to run system motion
    set_state(State::WorkAreaCalibration);
    // Disable X/Y soft limits while seeking
    if (config->_axes->_numberAxis > 0) config->_axes->_axis[X_AXIS]->_softLimits = false;
    if (config->_axes->_numberAxis > 1) config->_axes->_axis[Y_AXIS]->_softLimits = false;

    // Explicitly set X/Y machine positions to 0.0 without motion before each pass
    {
        float* cur   = get_mpos();
        float  newMP[MAX_N_AXIS];
        copyAxes(newMP, cur);
        if (config->_axes->_numberAxis > 0) newMP[X_AXIS] = 0.0f;
        if (config->_axes->_numberAxis > 1) newMP[Y_AXIS] = 0.0f;
        set_motor_steps_from_mpos(newMP);
        // Keep parser and planner in sync with the new machine position
        gc_sync_position();
        plan_sync_position();
        log_info("WorkAreaCalibration: set X/Y MPos to 0.0 before pass " << currentPass);
    }
    // Start positions are captured per-axis right when each seek begins (see nextStage)
        nextStage();
    }

    bool isCalibrating() {
        return stage != Stage::Idle && stage != Stage::Done;
    }

    void onLimit(LimitPin* limit) {
        if (!isCalibrating()) return;
        size_t axis = (size_t)limit->_axis;
        // Only act on the expected axis for the current stage to avoid cross-axis bounce
        if ((stage == Stage::XSeek && axis != X_AXIS) || (stage == Stage::YSeek && axis != Y_AXIS)) {
            return;
        }
    float* mpos = get_mpos();
        float  m    = mpos[axis];
    log_info("WorkAreaCalibration: limit tripped on axis " << axis << " at mpos=" << m);
        if (currentPass == 1) {
            if (axis == X_AXIS && !pass1x.captured) { pass1x.captured = true; pass1x.limit_mpos = m; }
            if (axis == Y_AXIS && !pass1y.captured) { pass1y.captured = true; pass1y.limit_mpos = m; }
        } else if (currentPass == 2 || currentPass == 3) {
            if (axis == X_AXIS && !pass2x.captured) { pass2x.captured = true; pass2x.limit_mpos = m; }
            if (axis == Y_AXIS && !pass2y.captured) { pass2y.captured = true; pass2y.limit_mpos = m; }
        }
        // Stop motion and replan remaining axis if any
        Stepper::reset();
        plan_reset();
        nextStage();

        if (stage == Stage::Done) {
            // Finish pass; re-enable soft limits
            if (config->_axes->_numberAxis > 0) config->_axes->_axis[X_AXIS]->_softLimits = true;
            if (config->_axes->_numberAxis > 1) config->_axes->_axis[Y_AXIS]->_softLimits = true;
            
            // Save current state (P1 or P2)
            saveCalibrationState();
            
            set_state(State::Idle);
            stage = Stage::Idle;
        }
    }

    void abortCalibration() {
        reset();
    if (config->_axes->_numberAxis > 0) config->_axes->_axis[X_AXIS]->_softLimits = true;
    if (config->_axes->_numberAxis > 1) config->_axes->_axis[Y_AXIS]->_softLimits = true;
    set_state(State::Idle);
    }
}
