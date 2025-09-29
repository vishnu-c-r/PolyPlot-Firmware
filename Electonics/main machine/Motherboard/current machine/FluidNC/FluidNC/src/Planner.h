// Copyright (c) 2011-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2009-2011 Simen Svale Skogsrud
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Config.h"  // MAX_N_AXIS
#include "GCode.h"   // CoolantState
#include "Types.h"   // AxisMask

// Forward declare dependencies
class Stepper;
struct PenPosition;

// Define planner data condition flags first
struct PlMotion {
    uint8_t rapidMotion : 1;
    uint8_t systemMotion : 1;    // Single motion. Circumvents planner state. Used by home/park.
    uint8_t noFeedOverride : 1;  // Motion does not honor feed override.
    uint8_t inverseTime : 1;     // Interprets feed rate value as inverse time when set.
};

// Define plan_line_data_t before including pen.h
struct plan_line_data_t {
    float        feed_rate;  // Desired feed rate for line motion. Value is ignored, if rapid motion.
    PlMotion     motion;     // Bitflag variable to indicate motion conditions. See defines above.
    CoolantState coolant;
    int32_t      line_number;         // Desired line number to report when executing.
    bool         is_jog;              // true if this was generated due to a jog command
    bool         limits_checked;      // true if soft limits already checked
    bool         use_exact_feedrate;  // If true, always use feed_rate for this move

    // Add pen tracking variables
    int prevPenNumber;  // Previous pen number before change
    int penNumber;      // Current/target pen number

    // Pen change motion control feedrates
    float approach_feedrate;  // Fast approach feed rate for tool change
    float precise_feedrate;   // Slower precise movement feed rate for actual pen change
};

// Now safe to include pen.h
#include "pen.h"

#include <cstdint>

// This struct stores a linear movement of a g-code block motion with its critical "nominal" values
// are as specified in the source g-code.
struct plan_block_t {
    // Fields used by the bresenham algorithm for tracing the line
    uint32_t steps[MAX_N_AXIS];  // Step count along each axis
    uint32_t step_event_count;   // The maximum step axis count and number of steps required to complete this block.
    uint8_t  direction_bits;     // The direction bit set for this block

    // Block condition data
    PlMotion     motion;       // Block bitflag motion conditions
    CoolantState coolant;      // Coolant state
    int32_t      line_number;  // Block line number for reporting

    // Remove old unused fields:
    // int32_t      pen;          // OLD: Remove this
    // uint32_t     Module_Axis_steps;  // OLD: Remove this

    // Fields used by the motion planner to manage acceleration
    float entry_speed_sqr;
    float max_entry_speed_sqr;
    float acceleration;
    float millimeters;

    // Rate limiting data
    float max_junction_speed_sqr;
    float rapid_rate;
    float programmed_rate;

    bool is_jog;

    // Add the new pen change tracking:
    int currentPenNumber;   // Current pen number
    int previousPenNumber;  // Previous pen number
};

extern float last_position[MAX_N_AXIS];

// Planner data prototype. Must be used when passing new motions to the planner.

void plan_init();

// Initialize and reset the motion plan subsystem
void plan_reset();         // Reset all
void plan_reset_buffer();  // Reset buffer only.

// Add a new linear movement to the buffer. target[MAX_N_AXIS] is the signed, absolute target position
// in millimeters. Feed rate specifies the speed of the motion. If feed rate is inverted, the feed
// rate is taken to mean "frequency" and would complete the operation in 1/feed_rate minutes.
// Returns true on success.
bool plan_buffer_line(float* target, plan_line_data_t* pl_data);

// Called when the current block is no longer needed. Discards the block and makes the memory
// availible for new blocks.
void plan_discard_current_block();

// Gets the planner block for the special system motion cases. (Parking/Homing)
plan_block_t* plan_get_system_motion_block();

// Gets the current block. Returns NULL if buffer empty
plan_block_t* plan_get_current_block();

// Increment block index with wrap-around
static uint8_t plan_next_block_index(uint8_t block_index);

// Called by step segment buffer when computing executing block velocity profile.
float plan_get_exec_block_exit_speed_sqr();

// Called by main program during planner calculations and step segment buffer during initialization.
float plan_compute_profile_nominal_speed(plan_block_t* block);

// Re-calculates buffered motions profile parameters upon a motion-based override change.
void plan_update_velocity_profile_parameters();

// Reset the planner position vector (in steps)
void plan_sync_position();

// Reinitialize plan with a partially completed block
void plan_cycle_reinitialize();

// Returns the number of available blocks are in the planner buffer.
uint8_t plan_get_block_buffer_available();

// Returns the status of the block ring buffer. True, if buffer is full.
uint8_t plan_check_full_buffer();

void plan_get_planner_mpos(float* target);

// Add function declaration
bool plan_buffer_pen_change(int new_pen, plan_line_data_t* pl_data);

// Estimate remaining execution time (seconds) of all planned (queued) blocks including current.
// This is a best-effort analytical calculation using trapezoid/triangular motion assumptions.
// Returns 0 if no motion queued or estimation not possible.
float plan_estimate_remaining_time_sec();

// Higher resolution estimate including partially executed current block; may sample stepper state.
// Falls back to plan_estimate_remaining_time_sec if stepper prep info unavailable.
float plan_estimate_remaining_time_with_current_sec();
