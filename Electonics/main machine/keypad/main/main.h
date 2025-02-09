#ifndef MAIN_H
#define MAIN_H

// Pin definitions (using existing ones)
#define BUTTON_UP 9
#define BUTTON_RIGHT 8
#define BUTTON_DOWN 1
#define BUTTON_LEFT 0
#define BUTTON_PLAYPAUSE 3
#define NEOPIXEL_PIN 10

// Configurable settings
#define JOG_FEEDRATE 10000      // Feedrate for jogging in mm/min
#define NUM_PIXELS 5          // Number of NeoPixels

// Add these constants after other #define statements
#define BUTTON_HOLD_DELAY 500        // Default hold delay for jog buttons
#define HOME_HOLD_DELAY 1000         // Longer hold delay for home command
#define SHORT_JOG_DISTANCE 1     // Distance for single click (mm)
#define LONG_JOG_DISTANCE 1000   // Distance for continuous jog (mm)

#define REPORT_BUFFER_LEN 128  // Reduced buffer size

#endif
