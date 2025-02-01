#include "HBot.h"

#include "../Machine/MachineConfig.h"
#include "../Limits.h"  // ambiguousLimit()
#include "../Machine/Homing.h"
#include "../Protocol.h"  // protocol_execute_realtime
#include <cmath>

namespace Kinematics {
    void HBot::group(Configuration::HandlerBase& handler) {}

    void HBot::init() {
        log_info("Kinematic system: " << name());

        config->_axes->_axis[Y_AXIS]->_motors[0]->limitOtherAxis(X_AXIS);
        config->_axes->_axis[X_AXIS]->_motors[0]->limitOtherAxis(Y_AXIS);
    }

    bool HBot::canHome(AxisMask axisMask) {
        // Make sure there are no axes that are not in homingMask
        if (axisMask && !(axisMask & Machine::Axes::homingMask)) {
            log_error("Not a homed axis:");
            return false;
        }

        if (ambiguousLimit()) {
            log_error("Ambiguous limit switch touching. Manually clear all switches");
            return false;
        }
        return true;
    }

    bool HBot::limitReached(AxisMask& axisMask, MotorMask& motors, MotorMask limited) {
        MotorMask toClear = axisMask & limited;
        clear_bits(axisMask, limited);
        clear_bits(motors, limited);
        
        // Make sure to release motors and clear limit state
        releaseMotors(axisMask, motors);
        
        // For HBot, when one limit is hit we need to ensure both motors handle it properly
        auto axes = config->_axes;
        if (limited) {
            axes->_axis[X_AXIS]->_motors[0]->unlimit();
            axes->_axis[Y_AXIS]->_motors[0]->unlimit();
        }
        
        return bool(toClear);
    }

    void HBot::releaseMotors(AxisMask axisMask, MotorMask motors) {
        auto axes   = config->_axes;
        auto n_axis = axes->_numberAxis;
        for (size_t axis = X_AXIS; axis < n_axis; axis++) {
            if (bitnum_is_true(axisMask, axis)) {
                axes->_axis[axis]->_motors[0]->unlimit();
            }
        }
    }

    bool HBot::cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) {
        auto n_axis = config->_axes->_numberAxis;
        float motors[n_axis];
        transform_cartesian_to_motors(motors, target);

        if (!pl_data->motion.rapidMotion) {
            float cartesian_distance = vector_distance(target, position, n_axis);
            float last_motors[n_axis];
            transform_cartesian_to_motors(last_motors, position);
            float motor_distance = vector_distance(motors, last_motors, n_axis);
            pl_data->feed_rate *= motor_distance / cartesian_distance;
        }

        return mc_move_motors(motors, pl_data);
    }

    void HBot::motors_to_cartesian(float* cartesian, float* motors, int n_axis) {
        cartesian[X_AXIS] = (motors[Y_AXIS] - motors[X_AXIS]);
        cartesian[Y_AXIS] = -motors[X_AXIS];
        for (int axis = Z_AXIS; axis < n_axis; axis++) {
            cartesian[axis] = motors[axis];
        }
    }

    bool HBot::transform_cartesian_to_motors(float* motors, float* cartesian) {
        motors[X_AXIS] = -cartesian[Y_AXIS];                     // A = -Y (unchanged)
        motors[Y_AXIS] = cartesian[X_AXIS] - cartesian[Y_AXIS];  // B = X - Y (removed negative from X)

        auto n_axis = config->_axes->_numberAxis;
        for (size_t axis = Z_AXIS; axis < n_axis; axis++) {
            motors[axis] = cartesian[axis];
        }
        return true;
    }

    namespace {
        KinematicsFactory::InstanceBuilder<HBot> registration("HBot");
    }
}  // namespace Kinematics
