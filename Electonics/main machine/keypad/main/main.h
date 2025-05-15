#pragma once

// Pin definitions
#define NEOPIXEL_PIN 10
#define BUTTON_UP 9
#define BUTTON_RIGHT 8
#define BUTTON_DOWN 1
#define BUTTON_LEFT 0
#define BUTTON_PLAYPAUSE 3

// Jog distances
#define SHORT_JOG_DISTANCE 1 // mm
#define LONG_JOG_DISTANCE 1000 // mm

// Button timing
#define BUTTON_HOLD_DELAY 750    // ms - reduced from original if needed
#define HOME_HOLD_DELAY 1000     // ms - reduced to 1 second for easier triggering

// Movement settings
#define JOG_FEEDRATE 10000

// Buffer size
#define REPORT_BUFFER_LEN 128


