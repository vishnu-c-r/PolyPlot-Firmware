// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2009-2011 Simen Svale Skogsrud
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// This file implements the motion control routines for FluidNC firmware.
// It defines safeMove, motion queuing, linear and arc moves, dwell and probing routines,
// as well as automatic pen change sequences.

#include "MotionControl.h"
#include "Machine/MachineConfig.h"
#include "Machine/Homing.h"    // run_cycles
#include "WebUI/ToolConfig.h"  // Tool configuration and position info
#include "Limits.h"            // Soft limits checking
#include "Report.h"            // Logging and error reporting
#include "Protocol.h"          // Command buffer synchronization and realtime execution
#include "Planner.h"           // Motion planner buffering
#include "I2SOut.h"            // I2S motor output support
#include "Platform.h"          // Core-specific functions and macros
#include "Settings.h"          // Global coordinate system information
#include "Pen.h"               // Pen flags and routines

#include <cmath>

#ifndef M_PI
#    define M_PI 3.14159265358979323846
#endif

// Global variable to track the inflight move (for cancellation).
static volatile void* mc_pl_data_inflight;

// -----------------------------------------------------------------------------
// safeMove:
// Executes a linear move using the given target, then updates the global machine
// position so subsequent moves are chained together.
// -----------------------------------------------------------------------------
static bool safeMove(plan_line_data_t* pl_data, float* target) {
    if (!mc_linear(target, pl_data, target))
        return false;
    copyAxes(gc_state.position, target);
    return true;
}

void mc_init() {
    mc_pl_data_inflight = NULL;
}

// Execute linear motor motion in absolute millimeter coordinates. Feed rate given in
// millimeters/second unless invert_feed_rate is true.
// Then the feed_rate means that the motion should be completed in (1 minute)/feed_rate time.
//
// NOTE: This operates in the motor space rather than cartesian space. If a cartesian linear motion
// is desired, please see mc_linear() which will translate from cartesian to motor operations via
// kinematics.
//
// NOTE: This is the primary gateway to the planner. All line motions, including arc line
// segments, must pass through this routine before being passed to the planner. The seperation of
// mc_linear and plan_buffer_line is done primarily to place non-planner-type functions from being
// in the planner and to let backlash compensation or canned cycle integration simple and direct.
// returns true if line was submitted to planner, or false if intentionally dropped.
bool mc_move_motors(float* target, plan_line_data_t* pl_data) {
    bool submitted_result = false;
    // store the plan data so it can be cancelled by the protocol system if needed
    mc_pl_data_inflight = pl_data;

    // If in check gcode mode, prevent motion by blocking planner. Soft limits still work.
    if (state_is(State::CheckMode)) {
        mc_pl_data_inflight = NULL;
        return submitted_result;  // Bail, if system abort.
    }
    // NOTE: Backlash compensation may be installed here. It will need direction info to track when
    // to insert a backlash line motion(s) before the intended line motion and will require its own
    // plan_check_full_buffer() and check for system abort loop. Also for position reporting
    // backlash steps will need to be also tracked, which will need to be kept at a system level.
    // There are likely some other things that will need to be tracked as well. However, we feel
    // that backlash compensation should NOT be handled by the firmware itself, because there are a myriad
    // of ways to implement it and can be effective or ineffective for different CNC machines. This
    // would be better handled by the interface as a post-processor task, where the original g-code
    // is translated and inserts backlash motions that best suits the machine.
    // NOTE: Perhaps as a middle-ground, all that needs to be sent is a flag or special command that
    // indicates to the firmware what is a backlash compensation motion, so that the move is executed
    // without updating the machine position values. Since the position values used by the g-code
    // parser and planner are separate from the system machine positions, this is doable.
    // If the buffer is full: good! That means we are well ahead of the robot.
    // Remain in this loop until there is room in the buffer.

    while (plan_check_full_buffer()) {
        protocol_auto_cycle_start();  // Auto-cycle start when buffer is full.

        // While we are waiting for room in the buffer, look for realtime
        // commands and other situations that could cause state changes.
        protocol_execute_realtime();
        if (sys.abort) {
            mc_pl_data_inflight = NULL;
            return submitted_result;  // Bail, if system abort.
        }
    }

    // Plan and queue motion into planner buffer
    if (mc_pl_data_inflight == pl_data) {
        plan_buffer_line(target, pl_data);
        submitted_result = true;
    }
    mc_pl_data_inflight = NULL;
    return submitted_result;
}

void mc_cancel_jog() {
    if (mc_pl_data_inflight != NULL && ((plan_line_data_t*)mc_pl_data_inflight)->is_jog) {
        mc_pl_data_inflight = NULL;
    }
}

// -----------------------------------------------------------------------------
// mc_linear / mc_linear_no_check:
// Convert absolute target positions into motor steps with optional soft limit checking.
// -----------------------------------------------------------------------------
static bool mc_linear_no_check(float* target, plan_line_data_t* pl_data, float* position) {
    return config->_kinematics->cartesian_to_motors(target, pl_data, position);
}

bool mc_linear(float* target, plan_line_data_t* pl_data, float* position) {
    if (!pl_data->is_jog && !pl_data->limits_checked)
        if (config->_kinematics->invalid_line(target))
            return false;
    return mc_linear_no_check(target, pl_data, position);
}

// -----------------------------------------------------------------------------
// mc_arc:
// Generates an arc motion by dividing the angular travel into small linear segments.
// Unneeded internal derivation details have been removed for brevity.
// -----------------------------------------------------------------------------
void mc_arc(float*            target,
            plan_line_data_t* pl_data,
            float*            position,
            float*            offset,
            float             radius,
            size_t            axis_0,
            size_t            axis_1,
            size_t            axis_linear,
            bool              is_clockwise_arc,
            int               pword_rotations) {
    float center[3] = { position[axis_0] + offset[axis_0], position[axis_1] + offset[axis_1], 0 };

    // The first two axes are the circle plane and the third is the orthogonal plane
    size_t caxes[3] = { axis_0, axis_1, axis_linear };
    if (config->_kinematics->invalid_arc(target, pl_data, position, center, radius, caxes, is_clockwise_arc)) {
        return;
    }

    // Radius vector from center to current location
    float radii[2] = { -offset[axis_0], -offset[axis_1] };
    float rt[2]    = { target[axis_0] - center[0], target[axis_1] - center[1] };

    auto n_axis = config->_axes->_numberAxis;

    float previous_position[n_axis] = { 0.0 };
    for (size_t i = 0; i < n_axis; i++) {
        previous_position[i] = position[i];
    }

    // CCW angle between position and target from circle center. Only one atan2() trig computation required.
    float angular_travel = atan2f(radii[0] * rt[1] - radii[1] * rt[0], radii[0] * rt[0] + radii[1] * rt[1]);
    if (is_clockwise_arc) {  // Correct atan2 output per direction
        if (angular_travel >= -ARC_ANGULAR_TRAVEL_EPSILON) {
            angular_travel -= 2 * float(M_PI);
        }
        // See https://linuxcnc.org/docs/2.6/html/gcode/gcode.html#sec:G2-G3-Arc
        // The P word specifies the number of extra rotations.  Missing P, P0 or P1
        // is just the programmed arc.  Pn adds n-1 rotations
        if (pword_rotations > 1) {
            angular_travel -= (pword_rotations - 1) * 2 * float(M_PI);
        }
    } else {
        if (angular_travel <= ARC_ANGULAR_TRAVEL_EPSILON) {
            angular_travel += 2 * float(M_PI);
        }
        if (pword_rotations > 1) {
            angular_travel += (pword_rotations - 1) * 2 * float(M_PI);
        }
    }

    // NOTE: Segment end points are on the arc, which can lead to the arc diameter being smaller by up to
    // (2x) arc_tolerance. For 99% of users, this is just fine. If a different arc segment fit
    // is desired, i.e. least-squares, midpoint on arc, just change the mm_per_arc_segment calculation.
    // For most uses, this value should not exceed 2000.
    uint16_t segments =
        uint16_t(floorf(fabsf(0.5 * angular_travel * radius) / sqrtf(config->_arcTolerance * (2 * radius - config->_arcTolerance))));
    if (segments) {
        // Multiply inverse feed_rate to compensate for the fact that this movement is approximated
        // by a number of discrete segments. The inverse feed_rate should be correct for the sum of
        // all segments.
        if (pl_data->motion.inverseTime) {
            pl_data->feed_rate *= segments;
            pl_data->motion.inverseTime = 0;  // Force as feed absolute mode over arc segments.
        }
        float theta_per_segment = angular_travel / segments;
        float linear_per_segment[n_axis];
        linear_per_segment[axis_linear] = (target[axis_linear] - position[axis_linear]) / segments;
        for (size_t i = A_AXIS; i < n_axis; i++) {
            linear_per_segment[i] = (target[i] - position[i]) / segments;
        }
        /* Vector rotation by transformation matrix: r is the original vector, r_T is the rotated vector,
           and phi is the angle of rotation. Solution approach by Jens Geisler.
               r_T = [cos(phi) -sin(phi);
                      sin(phi)  cos(phi] * r ;

           For arc generation, the center of the circle is the axis of rotation and the radius vector is
           defined from the circle center to the initial position. Each line segment is formed by successive
           vector rotations. Single precision values can accumulate error greater than tool precision in rare
           cases. So, exact arc path correction is implemented. This approach avoids the problem of too many very
           expensive trig operations [sin(),cos(),tan()] which can take 100-200 usec each to compute.

           Small angle approximation may be used to reduce computation overhead further. A third-order approximation
           (second order sin() has too much error) holds for most, if not, all CNC applications. Note that this
           approximation will begin to accumulate a numerical drift error when theta_per_segment is greater than
           ~0.25 rad(14 deg) AND the approximation is successively used without correction several dozen times. This
           scenario is extremely unlikely, since segment lengths and theta_per_segment are automatically generated
           and scaled by the arc tolerance setting. Only a very large arc tolerance setting, unrealistic for CNC
           applications, would cause this numerical drift error. However, it is best to set N_ARC_CORRECTION from a
           low of ~4 to a high of ~20 or so to avoid trig operations while keeping arc generation accurate.

           This approximation also allows mc_arc to immediately insert a line segment into the planner
           without the initial overhead of computing cos() or sin(). By the time the arc needs to be applied
           a correction, the planner should have caught up to the lag caused by the initial mc_arc overhead.
           This is important when there are successive arc motions.
        */
        // Computes: cos_T = 1 - theta_per_segment^2/2, sin_T = theta_per_segment - theta_per_segment^3/6) in ~52usec
        float cos_T = 2.0f - theta_per_segment * theta_per_segment;
        float sin_T = theta_per_segment * 0.16666667f * (cos_T + 4.0f);
        cos_T *= 0.5;
        float    sin_Ti, cos_Ti;
        uint16_t i;
        size_t   count             = 0;
        float    original_feedrate = pl_data->feed_rate;
        for (i = 1; i < segments; i++) {
            if (count < N_ARC_CORRECTION) {
                // Apply vector rotation matrix. ~40 usec
                float ri = radii[0] * sin_T + radii[1] * cos_T;
                radii[0] = radii[0] * cos_T - radii[1] * sin_T;
                radii[1] = ri;
                count++;
            } else {
                // Arc correction to radius vector. Computed only every N_ARC_CORRECTION increments. ~375 usec
                // Compute exact location by applying transformation matrix from initial radius vector(=-offset).
                cos_Ti   = cosf(i * theta_per_segment);
                sin_Ti   = sinf(i * theta_per_segment);
                radii[0] = -offset[axis_0] * cos_Ti + offset[axis_1] * sin_Ti;
                radii[1] = -offset[axis_0] * sin_Ti - offset[axis_1] * cos_Ti;
                count    = 0;
            }
            // Update arc_target location
            position[axis_0] = center[0] + radii[0];
            position[axis_1] = center[1] + radii[1];
            position[axis_linear] += linear_per_segment[axis_linear];
            for (size_t i = A_AXIS; i < n_axis; i++)
                position[i] += linear_per_segment[i];
            pl_data->feed_rate = original_feedrate;
            mc_linear(position, pl_data, previous_position);
            copyAxes(previous_position, position);
            if (sys.abort)
                return;
        }
    }
    mc_linear(target, pl_data, previous_position);
}

// -----------------------------------------------------------------------------
// mc_dwell:
// Pauses motion for a specified number of milliseconds.
// -----------------------------------------------------------------------------
bool mc_dwell(int32_t milliseconds) {
    if (milliseconds <= 0 || state_is(State::CheckMode))
        return false;
    protocol_buffer_synchronize();
    return dwell_ms(milliseconds, DwellMode::Dwell);
}

// -----------------------------------------------------------------------------
// Probing routines:
// mc_probe_cycle and mc_probe_oscillate execute a tool-length probe cycle.
// Redundant inline detail comments have been removed.
// -----------------------------------------------------------------------------
volatile bool probing;
bool          probe_succeeded = false;

// Perform tool length probe cycle. Requires probe switch.(normal probing)
// NOTE: Upon probe failure, the program will be stopped and placed into ALARM state.
GCUpdatePos mc_probe_cycle(float* target, plan_line_data_t* pl_data, bool away, bool no_error, uint8_t offsetAxis, float offset) {
    if (!config->_probe->exists()) {
        log_error("Probe pin is not configured");
        return GCUpdatePos::None;
    }
    // TODO: Need to update this cycle so it obeys a non-auto cycle start.
    if (state_is(State::CheckMode)) {
        return config->_probe->_check_mode_start ? GCUpdatePos::None : GCUpdatePos::Target;
    }
    // Finish all queued commands and empty planner buffer before starting probe cycle.
    protocol_buffer_synchronize();
    if (sys.abort) {
        return GCUpdatePos::None;  // Return if system reset has been issued.
    }

    config->_stepping->beginLowLatency();

    // Initialize probing control variables
    probe_succeeded = false;  // Re-initialize probe history before beginning cycle.
    config->_probe->set_direction(away);
    // After syncing, check if probe is already triggered. If so, halt and issue alarm.
    // NOTE: This probe initialization error applies to all probing cycles.
    if (config->_probe->tripped()) {
        send_alarm(ExecAlarm::ProbeFailInitial);
        protocol_execute_realtime();
        config->_stepping->endLowLatency();
        return GCUpdatePos::None;  // Nothing else to do but bail.
    }
    // Setup and queue probing motion. Auto cycle-start should not start the cycle.
    mc_linear(target, pl_data, gc_state.position);
    // Activate the probing state monitor in the stepper module.
    probing = true;
    // Perform probing cycle. Wait here until probe is triggered or motion completes.
    protocol_send_event(&cycleStartEvent);
    do {
        protocol_execute_realtime();
        if (sys.abort) {
            config->_stepping->endLowLatency();
            return GCUpdatePos::None;  // Check for system abort
        }
    } while (!state_is(State::Idle));
    config->_stepping->endLowLatency();

    // Probing cycle complete!
    // Set state variables and error out, if the probe failed and cycle with error is enabled.
    if (!probing) {
        if (no_error)
            get_motor_steps(probe_steps);
        probe_succeeded = true;
    } else {
        if (!no_error)
            send_alarm(ExecAlarm::ProbeFailContact);
    }
    probing = false;
    protocol_execute_realtime();
    Stepper::reset();
    plan_reset();
    plan_sync_position();
    if (MESSAGE_PROBE_COORDINATES)
        report_probe_parameters(allChannels);
    if (probe_succeeded) {
        if (offset != __FLT_MAX__) {
            float coord_data[MAX_N_AXIS];
            float probe_contact[MAX_N_AXIS];
            motor_steps_to_mpos(probe_contact, probe_steps);
            coords[gc_state.modal.coord_select]->get(coord_data);
            auto n_axis = config->_axes->_numberAxis;
            for (int axis = 0; axis < n_axis; axis++) {
                if (offsetAxis & (1 << axis)) {
                    coord_data[axis] = probe_contact[axis] - offset;
                    break;
                }
            }
            log_info("Probe offset applied:");
            coords[gc_state.modal.coord_select]->set(coord_data);
            copyAxes(gc_state.coord_system, coord_data);
            report_wco_counter = 0;
        }
        return GCUpdatePos::System;
    } else {
        return GCUpdatePos::Target;
    }
}

GCUpdatePos mc_probe_oscillate(float* target, plan_line_data_t* pl_data, bool away, bool no_error, uint8_t offsetAxis, float offset) {
    if (!config->_probe->exists()) {
        log_error("Probe pin is not configured");
        return GCUpdatePos::None;
    }
    if (state_is(State::CheckMode))
        return config->_probe->_check_mode_start ? GCUpdatePos::None : GCUpdatePos::Target;
    protocol_buffer_synchronize();
    if (sys.abort)
        return GCUpdatePos::None;
    config->_stepping->beginLowLatency();
    probe_succeeded = false;
    config->_probe->set_direction(away);
    if (config->_probe->tripped()) {
        send_alarm(ExecAlarm::ProbeFailInitial);
        protocol_execute_realtime();
        config->_stepping->endLowLatency();
        return GCUpdatePos::None;
    }
    float original_target[MAX_N_AXIS];
    memcpy(original_target, target, sizeof(original_target));
    float oscillation_amplitude = 2.0f;
    float oscillation_speed     = 200.0f;
    pl_data->feed_rate          = oscillation_speed;
    probing                     = true;
    float z_start               = gc_state.position[Z_AXIS];
    float z_end                 = target[Z_AXIS];
    int   z_steps               = 100;
    float z_step_size           = (z_start - z_end) / z_steps;
    for (int i = 0; i < z_steps; ++i) {
        target[Z_AXIS] = z_start - (z_step_size * (i + 1));
        target[X_AXIS] = original_target[X_AXIS] + ((i % 2 == 0) ? oscillation_amplitude : -oscillation_amplitude);
        mc_linear(target, pl_data, gc_state.position);
        protocol_send_event(&cycleStartEvent);
        do {
            protocol_execute_realtime();
            if (sys.abort) {
                config->_stepping->endLowLatency();
                return GCUpdatePos::None;
            }
            if (config->_probe->tripped()) {
                probe_succeeded = true;
                break;
            }
        } while (!state_is(State::Idle));
        if (probe_succeeded)
            break;
    }
    config->_stepping->endLowLatency();
    if (probe_succeeded) {
        if (no_error)
            get_motor_steps(probe_steps);
        else
            send_alarm(ExecAlarm::ProbeFailContact);
    }
    probing = false;
    protocol_execute_realtime();
    if (probe_succeeded) {
        if (offset != __FLT_MAX__) {
            float coord_data[MAX_N_AXIS];
            float probe_contact[MAX_N_AXIS];
            motor_steps_to_mpos(probe_contact, probe_steps);
            coords[gc_state.modal.coord_select]->get(coord_data);
            auto n_axis = config->_axes->_numberAxis;
            for (int axis = 0; axis < n_axis; axis++) {
                if (offsetAxis & (1 << axis)) {
                    coord_data[axis] = probe_contact[axis] - offset;
                    break;
                }
            }
            log_info("Probe offset applied:");
            coords[gc_state.modal.coord_select]->set(coord_data);
            copyAxes(gc_state.coord_system, coord_data);
            report_wco_counter = 0;
        }
        return GCUpdatePos::System;
    } else {
        return GCUpdatePos::Target;
    }
}

// -----------------------------------------------------------------------------
// mc_override_ctrl_update:
// Synchronizes and updates the system override control state.
// -----------------------------------------------------------------------------
void mc_override_ctrl_update(Override override_state) {
    protocol_buffer_synchronize();
    if (sys.abort)
        return;
    sys.override_ctrl = override_state;
}

// -----------------------------------------------------------------------------
// mc_critical:
// Resets steppers and sends an alarm when a critical condition is detected.
// -----------------------------------------------------------------------------
void mc_critical(ExecAlarm alarm) {
    if (inMotionState() || sys.step_control.executeHold || sys.step_control.executeSysMotion)
        Stepper::reset();
    send_alarm(alarm);
}

// -----------------------------------------------------------------------------
// Automatic pen change routines:
// Implements the pen change sequence using mc_pen_change, mc_pick_pen, and mc_drop_pen.
// -----------------------------------------------------------------------------
bool mc_pen_change(plan_line_data_t* pl_data) {
    static int current_loaded_pen = 0;
    int        nextPen            = pl_data->penNumber;

    float original_feed_rate = pl_data->feed_rate;
    float approach_feedrate  = pl_data->approach_feedrate;
    float precise_feedrate   = pl_data->precise_feedrate;

    auto& toolConfig = WebUI::ToolConfig::getInstance();
    if (!toolConfig.loadConfig()) {
        log_error("Failed to load tool config");
        return false;
    }

    float currentPos[MAX_N_AXIS], startPos[MAX_N_AXIS];
    copyAxes(currentPos, gc_state.position);
    copyAxes(startPos, currentPos);

    // Use fast approach feed rate for initial moves
    pl_data->feed_rate = approach_feedrate;

    // During pen change, bypass soft-limit checks and widen allowable travel envelope.
    // We also assert the global pen_change flag BEFORE any motion so limit math (if consulted)
    // and any UI restrictions apply consistently for the whole sequence.
    pl_data->limits_checked = true;  // Skip soft limit validation in mc_linear/invalid_line
    pen_change              = true;

    // Move to safe Z height first
    currentPos[Z_AXIS] = 0;
    if (!safeMove(pl_data, currentPos))
        return false;
    if (current_loaded_pen > 0 && (current_loaded_pen == nextPen || nextPen == 0)) {
        if (!mc_drop_pen(pl_data, current_loaded_pen, startPos))
            return false;
        current_loaded_pen = 0;
        log_info("Pen redocked and cleared: " << nextPen);
    } else if (current_loaded_pen == 0 && nextPen > 0) {
        if (!mc_pick_pen(pl_data, nextPen, startPos))
            return false;
        current_loaded_pen = nextPen;
    } else if (current_loaded_pen > 0 && nextPen > 0 && current_loaded_pen != nextPen) {
        if (!mc_drop_pen(pl_data, current_loaded_pen, startPos))
            return false;
        pl_data->feed_rate = approach_feedrate;
        if (!mc_pick_pen(pl_data, nextPen, startPos))
            return false;
        current_loaded_pen = nextPen;
    }

    // Restore original feed rate
    pl_data->feed_rate = original_feed_rate;
    log_info("Pen change complete: " << current_loaded_pen);
    protocol_buffer_synchronize();
    plan_sync_position();
    pen_change = false;
    return true;
}

bool mc_pick_pen(plan_line_data_t* pl_data, int penNumber, float startPos[MAX_N_AXIS]) {
    float original_feed_rate = pl_data->feed_rate;
    float approach_feedrate  = pl_data->approach_feedrate;
    float precise_feedrate   = pl_data->precise_feedrate;

    float targetPos[MAX_N_AXIS];
    copyAxes(targetPos, gc_state.position);
    float pickupPos[MAX_N_AXIS];
    auto& toolConfig = WebUI::ToolConfig::getInstance();
    if (!toolConfig.getToolPosition(penNumber, pickupPos)) {
        log_error("Invalid pen pickup position");
        return false;
    }

    // Pen pickup sequence:
    // Use approach_feedrate for initial moves
    pl_data->use_exact_feedrate = false;
    pl_data->feed_rate          = approach_feedrate;
    protocol_buffer_synchronize();
    plan_sync_position();

    targetPos[Z_AXIS] = -1.0f;
    if (!safeMove(pl_data, targetPos))
        return false;

    targetPos[Y_AXIS] = pickupPos[Y_AXIS];
    if (!safeMove(pl_data, targetPos))
        return false;

    targetPos[X_AXIS] = -440.0f;
    if (!safeMove(pl_data, targetPos))
        return false;

    // Switch to precise_feedrate when moving past X=-440 boundary
    pl_data->use_exact_feedrate = true;
    pl_data->feed_rate          = precise_feedrate;
    protocol_buffer_synchronize();
    plan_sync_position();

    targetPos[X_AXIS] = pickupPos[X_AXIS];
    if (!safeMove(pl_data, targetPos))
        return false;

    targetPos[Z_AXIS] = pickupPos[Z_AXIS];
    if (!safeMove(pl_data, targetPos))
        return false;

    targetPos[X_AXIS] = -480.0f;
    if (!safeMove(pl_data, targetPos))
        return false;

    targetPos[Z_AXIS] = 0.0f;
    if (!safeMove(pl_data, targetPos))
        return false;

    targetPos[X_AXIS] = -440.0f;
    if (!safeMove(pl_data, targetPos))
        return false;

    // Use approach_feedrate again for exit move
    pl_data->use_exact_feedrate = false;
    pl_data->feed_rate          = approach_feedrate;
    protocol_buffer_synchronize();
    plan_sync_position();

    // targetPos[X_AXIS] = -440.0f;
    // if (!safeMove(pl_data, targetPos))
    //     return false;

    pl_data->feed_rate = original_feed_rate;
    return true;
}

bool mc_drop_pen(plan_line_data_t* pl_data, int penNumber, float startPos[MAX_N_AXIS]) {
    float original_feed_rate = pl_data->feed_rate;
    float approach_feedrate  = pl_data->approach_feedrate;
    float precise_feedrate   = pl_data->precise_feedrate;

    float targetPos[MAX_N_AXIS];
    copyAxes(targetPos, gc_state.position);
    float dropPos[MAX_N_AXIS];
    auto& toolConfig = WebUI::ToolConfig::getInstance();
    if (!toolConfig.getToolPosition(penNumber, dropPos)) {
        log_error("Invalid pen drop position");
        return false;
    }

    // Pen drop sequence:

    // Use approach_feedrate for initial moves
    pl_data->use_exact_feedrate = false;
    pl_data->feed_rate          = approach_feedrate;
    protocol_buffer_synchronize();
    plan_sync_position();

    targetPos[Z_AXIS] = -1.0f;
    if (!safeMove(pl_data, targetPos))
        return false;

    targetPos[Y_AXIS] = dropPos[Y_AXIS];
    if (!safeMove(pl_data, targetPos))
        return false;

    targetPos[X_AXIS] = -440.0f;
    if (!safeMove(pl_data, targetPos))
        return false;

    // Switch to precise_feedrate when moving past X=-440 boundary
    pl_data->use_exact_feedrate = true;
    pl_data->feed_rate          = precise_feedrate;
    protocol_buffer_synchronize();
    plan_sync_position();

    targetPos[X_AXIS] = -480.0f;
    if (!safeMove(pl_data, targetPos))
        return false;

    targetPos[Z_AXIS] = dropPos[Z_AXIS];
    if (!safeMove(pl_data, targetPos))
        return false;

    targetPos[X_AXIS] = dropPos[X_AXIS];
    if (!safeMove(pl_data, targetPos))
        return false;

    targetPos[Z_AXIS] = -1.0f;
    if (!safeMove(pl_data, targetPos))
        return false;

    // After pen drop, use precise/slow feedrate for the next move
    pl_data->use_exact_feedrate = true;
    pl_data->feed_rate          = precise_feedrate;
    protocol_buffer_synchronize();
    plan_sync_position();

    targetPos[X_AXIS] = -440.0f;
    if (!safeMove(pl_data, targetPos))
        return false;

    pl_data->feed_rate = original_feed_rate;
    // toolConfig.setToolOccupied(penNumber, true);
    return true;
}
