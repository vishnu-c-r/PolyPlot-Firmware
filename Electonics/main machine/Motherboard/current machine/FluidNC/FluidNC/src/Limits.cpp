#include "Limits.h"

#include "Machine/MachineConfig.h"
#include "MotionControl.h"  // mc_critical
#include "System.h"         // sys.*
#include "Protocol.h"       // protocol_execute_realtime
#include "Platform.h"       // WEAK_LINK
#include "Machine/Axis.h"
#include "GCode.h"  // laser_offset_disabled

#include <freertos/task.h>
#include <freertos/queue.h>
#include <atomic>  // fence

xQueueHandle limit_sw_queue;  // used by limit switch debouncing

void limits_init() {
#ifdef LATER  // We need to rethink debouncing
    if (Machine::Axes::limitMask) {
        if (limit_sw_queue == NULL && config->_softwareDebounceMs != 0) {
            // setup task used for debouncing
            if (limit_sw_queue == NULL) {
                limit_sw_queue = xQueueCreate(10, sizeof(int));
                xTaskCreate(limitCheckTask,
                            "limitCheckTask",
                            2048,
                            NULL,
                            5,  // priority
                            NULL);
            }
        }
    }
#endif
}

// Returns limit state as a bit-wise uint32 variable. Each bit indicates an axis limit, where
// triggered is 1 and not triggered is 0. Invert mask is applied. Axes are defined by their
// number in bit position, i.e. Z_AXIS is bitnum_to_mask(2), and Y_AXIS is bitnum_to_mask(1).
// The lower 16 bits are used for motor0 and the upper 16 bits are used for motor1 switches
MotorMask limits_get_state() {
    return Machine::Axes::posLimitMask | Machine::Axes::negLimitMask;
}

bool limits_startup_check() {  // return true if there is a hard limit error.
    MotorMask lim_pin_state = limits_get_state();
    if (lim_pin_state) {
        auto n_axis = config->_axes->_numberAxis;
        for (size_t axis = 0; axis < n_axis; axis++) {
            for (size_t motor = 0; motor < 2; motor++) {
                if (bitnum_is_true(lim_pin_state, Machine::Axes::motor_bit(axis, motor))) {
                    log_warn("Active limit switch on " << config->_axes->axisName(axis) << " axis motor " << motor);
                }
            }
        }
    }
    return (config->_start->_checkLimits && (config->_axes->hardLimitMask() & lim_pin_state));
}

// Called only from Kinematics canHome() methods, hence from states allowing homing
bool ambiguousLimit() {
    if (Machine::Axes::posLimitMask & Machine::Axes::negLimitMask) {
        mc_critical(ExecAlarm::HomingAmbiguousSwitch);
        return true;
    }
    return false;
}

// These functions control soft limits behavior and integrate pen_change flag

bool soft_limit = false;

// Define pen_change as volatile since it's accessed from multiple tasks
volatile bool pen_change = false;  // Globally controlled flag to restrict travel during pen/tool changes

// Calculate maximum allowed position based on configured limits and current mode
float limitsMaxPosition(size_t axis) {
    auto axisConfig = config->_axes->_axis[axis];
    auto homing     = axisConfig->_homing;
    auto mpos       = homing ? homing->_mpos : 0;

    // When pen_change mode is active, use restricted travel limits for X and Y axes
    // This prevents accidental collisions during tool changes
    auto maxtravel = (pen_change && axis != Z_AXIS) ? axisConfig->_penChangeTravel : axisConfig->_maxTravel;

    // Check if custom work area limits are configured and should be applied
    // Work area limits are disabled during:
    // 1. pen_change operations (tool changing)
    // 2. Normal job execution (when machine is in Cycle state)
    // Work area limits are ENABLED during:
    // - Manual jogging and alignment (Idle state)
    // - Setup operations with laser pointer
    if (config->useWorkAreaLimits() && !pen_change && !state_is(State::Cycle)) {
        if (axis == X_AXIS) {
            return config->getWorkAreaMaxX();
        } else if (axis == Y_AXIS) {
            return config->getWorkAreaMaxY();
        }
        // For Z axis and other axes, fall through to normal calculation
    }

    return (!homing || homing->_positiveDirection) ? mpos : mpos + maxtravel;
}

// Calculate minimum allowed position based on configured limits and current mode
float limitsMinPosition(size_t axis) {
    auto axisConfig = config->_axes->_axis[axis];
    auto homing     = axisConfig->_homing;
    auto mpos       = homing ? homing->_mpos : 0;

    // Apply pen_change travel restriction to minimum positions
    auto maxtravel = (pen_change && axis != Z_AXIS) ? axisConfig->_penChangeTravel : axisConfig->_maxTravel;

    // Check if custom work area limits are configured and should be applied
    // Work area limits are disabled during:
    // 1. pen_change operations (tool changing)
    // 2. Normal job execution (when machine is in Cycle state)
    // Work area limits are ENABLED during:
    // - Manual jogging and alignment (Idle state)
    // - Setup operations with laser pointer
    if (config->useWorkAreaLimits() && !pen_change && !state_is(State::Cycle)) {
        if (axis == X_AXIS) {
            return config->getWorkAreaMinX();
        } else if (axis == Y_AXIS) {
            return config->getWorkAreaMinY();
        }
        // For Z axis and other axes, fall through to normal calculation
    }

    return (!homing || homing->_positiveDirection) ? mpos - maxtravel : mpos;
}

// Performs a soft limit check. Called from mcline() only. Assumes the machine has been homed,
// the workspace volume is in all negative space, and the system is in normal operation.
// NOTE: Used by jogging to limit travel within soft-limit volume.
void limit_error(size_t axis, float coordinate) {
    log_info("Soft limit on " << Machine::Axes::_names[axis] << " target:" << coordinate);

    limit_error();
}

void limit_error() {
    soft_limit = true;
    // Force feed hold if cycle is active. All buffered blocks are guaranteed to be within
    // workspace volume so just come to a controlled stop so position is not lost. When complete
    // enter alarm mode.
    protocol_buffer_synchronize();
    if (state_is(State::Cycle)) {
        protocol_send_event(&feedHoldEvent);
        do {
            protocol_execute_realtime();
            if (sys.abort) {
                return;
            }
        } while (!state_is(State::Idle));
    }

    mc_critical(ExecAlarm::SoftLimit);
}
