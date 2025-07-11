// Copyright (c) 2022 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Parking.h"
#include "System.h"                 // sys
#include "Stepper.h"                // Stepper::
#include "Machine/MachineConfig.h"  // config

// Plans and executes the single special motion case for parking. Independent of main planner buffer.
// NOTE: Uses the always free planner ring buffer head to store motion parameters for execution.
void Parking::moveto(float* target) {
    if (sys.abort) {
        return;  // Block during abort.
    }
    if (plan_buffer_line(target, &plan_data)) {
        sys.step_control.executeSysMotion = true;
        sys.step_control.endMotion        = false;  // Allow parking motion to execute, if feed hold is active.
        Stepper::parking_setup_buffer();            // Setup step segment buffer for special parking motion case
        Stepper::prep_buffer();
        Stepper::wake_up();
        do {
            protocol_exec_rt_system();
            if (sys.abort) {
                return;
            }
        } while (sys.step_control.executeSysMotion);
        Stepper::parking_restore_buffer();  // Restore step segment buffer to normal run state.
    } else {
        sys.step_control.executeSysMotion = false;
        protocol_exec_rt_system();
    }
}

bool Parking::can_park() {
    if (!_enable) {
        return false;
    }
    if (bitnum_is_false(Machine::Axes::homingMask, _axis)) {
        // No parking without homing
        return false;
    }
    if (!config->_enableParkingOverrideControl) {
        // _enableParkingOverrideControl adds M56 whereby you can
        // disable parking via GCode.  If that feature is not present,
        // parking is enabled subject to the preceding tests.
        return true;
    }
    // If the M56 feature is present, M56 controls the value
    // of sys.override_ctrl, thus letting you disable parking
    // by saying M56 P0
    return sys.override_ctrl == Override::ParkingMotion;
}

void Parking::setup() {
    // Initialize parking local variables
    retract_waypoint = _pullout;
    memset(&plan_data, 0, sizeof(plan_line_data_t));
    plan_data.motion                = {};
    plan_data.motion.systemMotion   = 1;
    plan_data.motion.noFeedOverride = 1;
    plan_data.line_number           = PARKING_MOTION_LINE_NUMBER;
    plan_data.is_jog                = false;
    block                           = plan_get_current_block();

    if (block) {
        saved_coolant = block->coolant;
    } else {
        saved_coolant = gc_state.modal.coolant;
    }
}

void Parking::set_target() {
    copyAxes(parking_target, get_mpos());
}

void Parking::park(bool restart) {
    if (!restart) {
        // Get current position and store restore location and retract waypoint.
        copyAxes(restore_target, parking_target);
        retract_waypoint += restore_target[_axis];
        retract_waypoint = MIN(retract_waypoint, _target_mpos);
    }

    if (can_park() && parking_target[_axis] < _target_mpos) {
        // Retract spindle by pullout distance. Ensure retraction motion moves away from
        // the workpiece and waypoint motion doesn't exceed the parking target location.
        if (parking_target[_axis] < retract_waypoint) {
            log_debug("Parking pullout");
            parking_target[_axis] = retract_waypoint;
            plan_data.feed_rate   = _pullout_rate;
            plan_data.coolant     = saved_coolant;
            moveto(parking_target);
        }

        // NOTE: Clear accessory state after retract and after an aborted restore motion.
        plan_data.coolant               = {};
        plan_data.motion                = {};
        plan_data.motion.systemMotion   = 1;
        plan_data.motion.noFeedOverride = 1;

        gc_ovr_changed();

        // Execute fast parking retract motion to parking target location.
        if (parking_target[_axis] < _target_mpos) {
            log_debug("Parking motion");
            parking_target[_axis] = _target_mpos;
            plan_data.feed_rate   = _rate;
            moveto(parking_target);
        }
    } else {
        log_debug("Spin down only");
        config->_coolant->off();
        gc_ovr_changed();
    }
}
void Parking::unpark(bool restart) {
    // Execute fast restore motion to the pull-out position. Parking requires homing enabled.
    // NOTE: State is will remain DOOR, until the de-energizing and retract is complete.
    if (can_park()) {
        // Check to ensure the motion doesn't move below pull-out position.
        if (parking_target[_axis] <= _target_mpos) {
            log_debug("Parking return to pullout position");
            parking_target[_axis] = retract_waypoint;
            plan_data.feed_rate   = _rate;
            moveto(parking_target);
        }
    }

    // Restore coolant state.
    if (gc_state.modal.coolant.Flood || gc_state.modal.coolant.Mist) {
        // Block if safety door re-opened during prior restore actions.
        if (!restart) {
            restore_coolant();
            gc_ovr_changed();
        }
    }

    // Execute slow plunge motion from pull-out position to resume position.
    if (can_park()) {
        // Block if safety door re-opened during prior restore actions.
        if (!restart) {
            log_debug("Parking restore original state");
            // Whether or not a retraction happened, returning to the original
            // position should be valid, whether it moves or not.
            plan_data.feed_rate = _pullout_rate;
            plan_data.coolant   = saved_coolant;
            moveto(restore_target);
        }
    }
}

void Parking::restore_coolant() {
    config->_coolant->set_state(saved_coolant);
}

void Parking::group(Configuration::HandlerBase& handler) {
    handler.item("enable", _enable);
    handler.item("axis", _axis, axisType);
    handler.item("target_mpos_mm", _target_mpos);
    handler.item("rate_mm_per_min", _rate);
    handler.item("pullout_distance_mm", _pullout, 0, 3e38);
    handler.item("pullout_rate_mm_per_min", _pullout_rate);
}
