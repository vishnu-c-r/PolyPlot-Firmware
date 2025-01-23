#pragma once
#include "Realtime.h"  // Add this to get realtime_cmd_t

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>  // Add this for size_t

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
extern int milliseconds();
extern void delay(unsigned long ms);

// Optional debug

// Weak callback functions
extern void show_state(const char* state);
extern void handle_msg(char* command, char* arguments);
extern void show_error(int error);
extern void show_alarm(int alarm);
extern void show_ok();
extern void show_timeout();

// Add these extern declarations
extern bool _ackwait;
extern int _ack_time_limit;
extern bool _alarm14;

// Additional required function declarations
extern void show_versions(const char* grbl_version, const char* fluidnc_version);
extern void begin_status_report();
extern void end_status_report();
extern void show_limits(bool probe, const bool* limits, size_t n_axis);
extern void show_dro(const pos_t* axes, const pos_t* wcos, bool isMpos, bool* limits, size_t n_axis);
extern void show_file(const char* filename, file_percent_t percent);
extern void show_spindle_coolant(int spindle, bool flood, bool mist);
extern void show_feed_spindle(uint32_t feedrate, uint32_t spindle_speed);
extern void show_overrides(override_percent_t feed_ovr, override_percent_t rapid_ovr, override_percent_t spindle_ovr);
extern void show_probe(const pos_t* axes, const bool probe_success, size_t n_axis);
extern void show_offset(offset_t offset, const pos_t* axes, size_t n_axis);
extern void show_probe_pin(bool on);
extern void show_control_pins(const char* pins);
extern void handle_json(const char* line);
extern void handle_signon(char* version, char* extra);
extern void handle_other(char* line);

#ifdef __cplusplus
}
#endif