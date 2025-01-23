#pragma once

#include "Kinematics.h"
#include "Cartesian.h"

namespace Kinematics {
    class HBot : public Cartesian {
    public:
        HBot() = default;

        HBot(const HBot&) = delete;
        HBot(HBot&&)      = delete;
        HBot& operator=(const HBot&) = delete;
        HBot& operator=(HBot&&) = delete;

        // Kinematic Interface

        virtual void init() override;
        bool         cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) override;
        void         motors_to_cartesian(float* cartesian, float* motors, int n_axis) override;

        bool canHome(AxisMask axisMask) override;
        void releaseMotors(AxisMask axisMask, MotorMask motors) override;
        bool limitReached(AxisMask& axisMask, MotorMask& motors, MotorMask limited);

        // Configuration handlers:
        void         validate() override {}
        virtual void group(Configuration::HandlerBase& handler) override;
        void         afterParse() override {}

        bool transform_cartesian_to_motors(float* motors, float* cartesian) override;

        // Name of the configurable. Must match the name registered in the cpp file.
        virtual const char* name() const override { return "HBot"; }

        ~HBot() {}

    private:
        void lengths_to_xy(float left_length, float right_length, float& x, float& y);
        void xy_to_lengths(float x, float y, float& left_length, float& right_length);

        void plan_homing_move(AxisMask axisMask, bool approach, bool seek);
    };
}  // namespace Kinematics
