#pragma once
#include "Realtime.h"  // Add this to get realtime_cmd_t

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>  // Add this for size_t
#include <Arduino.h>

#define REPORT_BUFFER_LEN 128  // Reduced buffer size

// Axis definitions
#define MAX_N_AXIS 6
#define X_AXIS 0
#define Y_AXIS 1
#define Z_AXIS 2
#define A_AXIS 3
#define B_AXIS 4
#define C_AXIS 5

// Type definitions
typedef float pos_t;
typedef int32_t feedrate_t;
typedef uint32_t override_percent_t;
typedef int32_t file_percent_t;

// Basic commands needed
// Remove the existing enum definition here

// GCode offsets
typedef enum {
    G54  = 1,
    G55  = 2,
    G56  = 3,
    G57  = 4,
    G58  = 5,
    G59  = 6,
    G28  = 7,
    G30  = 8,
    G92  = 9,
} offset_t;

// GCode modes structure
struct gcode_modes {
    const char* modal;
    const char* wcs;
    const char* plane;
    const char* units;
    const char* distance;
    const char* program;
    const char* spindle;
    const char* mist;
    const char* flood;
    const char* parking;
    int         tool;
    uint32_t    spindle_speed;
    feedrate_t  feed;
};

// Core functions
void fnc_wait_ready();
void fnc_poll();
void fnc_send_line(const char* line, int timeout_ms);
void fnc_realtime(realtime_cmd_t c);  // This now uses realtime_cmd_t from Realtime.h

// Required implementations
extern int fnc_getchar();
extern void fnc_putchar(uint8_t ch);

// Weak callback functions
extern void show_state(const char* state);
extern void show_error(int error);
extern void show_alarm(int alarm);
extern void show_ok();

// Add these extern declarations
extern bool _ackwait;
extern int _ack_time_limit;
extern bool _alarm14;
extern bool _machine_ready; // New flag to detect READY message

#ifdef __cplusplus
}
#endif