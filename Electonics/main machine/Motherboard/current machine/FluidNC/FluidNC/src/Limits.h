// Copyright (c) 2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2009-2011 Simen Svale Skogsrud
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "System.h"
#include "Config.h"  // MAX_N_AXIS
#include "Error.h"   // Error type
#include "Types.h"   // MotorMask

#include <cstdint>

// Global state variables
extern bool soft_limit;    // Indicates if a soft limit violation has occurred
extern volatile bool pen_change;  

/**
 * @brief Initialize the limits subsystem
 * Sets up limit switch interrupts and debouncing if configured
 */
void limits_init();

/**
 * @brief Get the current state of all limit switches
 * @return MotorMask where each bit represents a limit switch state
 *         (1=triggered, 0=not triggered)
 */
MotorMask limits_get_state();

/**
 * @brief Check limit switches during system startup
 * @return true if a hard limit is triggered and limit checking is enabled
 */
bool limits_startup_check();

/**
 * @brief Handle a limit error condition
 * Called when a soft limit is violated or limit switch is hit
 */
void limit_error();

/**
 * @brief Handle axis-specific limit error
 * @param axis The axis number where the limit was exceeded
 * @param position The position that triggered the limit
 */
void limit_error(size_t axis, float position);

/**
 * @brief Get maximum allowed position for an axis
 * Takes into account homing status and pen_change restrictions
 * @param axis The axis number to check
 * @return The maximum allowed position in mm
 */
float limitsMaxPosition(size_t axis);

/**
 * @brief Get minimum allowed position for an axis
 * Takes into account homing status and pen_change restrictions
 * @param axis The axis number to check
 * @return The minimum allowed position in mm
 */
float limitsMinPosition(size_t axis);

// Private

// Returns limit state under mask
AxisMask limits_check(AxisMask check_mask);

// A task that runs after a limit switch interrupt.
void limitCheckTask(void* pvParameters);

bool limitsCheckTravel(float* target);

// True if an axis is reporting engaged limits on both ends.  This
// typically happens when the same pin is used for a pair of switches,
// so you cannot tell which one is triggered.  In that case, automatic
// pull-off is impossible.
bool ambiguousLimit();
