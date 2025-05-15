// Copyright (c) 2011-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2009-2011 Simen Svale Skogsrud
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
  Stepper.h - stepper motor driver: executes motion plans of planner.c using the stepper motors
*/

#include "Types.h"
#include "Pin.h"
#include "EnumItem.h"

#include <cstdint>

// Change from namespace to class to match Planner.h declaration
class Stepper {
public:
    static void  init();
    static bool  update_plan_block_parameters();  // Changed to return bool
    static bool  pulse_func();                    // Changed to return bool
    static void  reset();
    static void  wake_up();
    static void  go_idle();
    static void  stop_stepping();
    static void  parking_setup_buffer();
    static void  parking_restore_buffer();
    static void  prep_buffer();
    static float get_realtime_rate();

    static uint32_t isr_count;
};
