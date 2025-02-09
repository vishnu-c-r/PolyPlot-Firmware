#pragma once

#ifdef __cplusplus
#include <Adafruit_NeoPixel.h>

// Physical LED mapping on device
#define LED_UP 0        
#define LED_RIGHT 4     
#define LED_DOWN 2      
#define LED_LEFT 1      
#define LED_PLAYPAUSE 3 

// Animation timing parameters (all values in milliseconds)
#define BLINK_INTERVAL_MS 333      // Period for status blinks
#define BREATHING_RISE 2000        // Time to reach full brightness
#define BREATHING_FALL 1000        // Time to fade to minimum
#define HOMING_UPDATE_INTERVAL 15  // Animation frame time

// Default LED settings
#define NUM_PIXELS 5
#define DEFAULT_BRIGHTNESS 70

// Add new timing constants
#define FADE_INTERVAL_MS 50        // For breathing effect
#define FLICKER_INTERVAL_MS 100    // For orange flicker

#define ON_TIME 700   // Time in ms for which the LED is on
#define OFF_TIME 300  // Time in ms for which the LED is off

// Homing animation settings
#define HOMING_TRANSITION_STEP 5     // Step size for smooth transition
#define HOMING_BRIGHTNESS_PERIOD 2000 // Brightness modulation period in ms
#define HOMING_MIN_BRIGHTNESS 20      // Minimum brightness for homing

// Paused animation settings
#define PAUSED_BREATHING_RISE 2000   // Breathing rise time in ms
#define PAUSED_BREATHING_FALL 1000   // Breathing fall time in ms
#define PAUSED_MIN_BRIGHTNESS 5      // Minimum brightness for paused

namespace LEDControl {
    class LedColors {
    private:
        static Adafruit_NeoPixel* pixelsPtr;
        static unsigned long initStart;  // Add this declaration

    public:
        // New LED scheme
        static uint32_t COLOR_RED;    // For startup and errors
        static uint32_t COLOR_GREEN;  // For ready state
        static uint32_t COLOR_ORANGE; // For paused state
        static uint32_t COLOR_OFF;    // For inactive LEDs

        // Animation timing constants
        static const uint16_t STARTUP_FADE_TIME = 1000;   // 1 second fade
        static const uint16_t HOMING_CYCLE_TIME = 500;    // 0.5 seconds per cycle
        static const uint16_t READY_BLINK_TIME = 333;     // 3 blinks per second
        static const uint8_t READY_BLINK_COUNT = 3;       // Number of blinks
        static const uint16_t BREATHING_RISE = 2000;      // 2 second rise
        static const uint16_t BREATHING_FALL = 1000;      // 1 second fall
        static const uint16_t FLICKER_INTERVAL = 100;     // 0.1 second flicker
        
        static void init(Adafruit_NeoPixel& pixels);  // Declaration only

        // Add new animation states
        static bool inStartupMode;
        static bool inHomingMode;
        static uint8_t blinkCount;
        static uint8_t fadeValue;
        static int8_t fadeStep;
        
        // New animation methods for machine initialization:
        static void machineInitAnimation();
        static void transitionToReadyAnimation();
        
        // Add new animation methods
        static void startupAnimation();
        static void homingAnimation();
        static void readyBlinkAnimation();
        static void runningAnimation(bool flicker);
        static void pausedAnimation();
        // New alarm animation for flashing red.
        static void alarmAnimation();
        
        // Add timing variables
        static unsigned long lastAnimationUpdate;
        static unsigned long flickerTimer;

        // Add these missing function declarations
        static void idleAnimation();
        static void transitionStateColor(uint32_t fromColor, uint32_t toColor, uint16_t duration);
    };
}
#endif
