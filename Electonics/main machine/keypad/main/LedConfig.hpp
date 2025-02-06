#pragma once

#ifdef __cplusplus
#include <Adafruit_NeoPixel.h>

// LED positions - Original mapping
#define LED_UP 0        
#define LED_RIGHT 4     // Keep at 4
#define LED_DOWN 2      
#define LED_LEFT 1      // Keep at 1
#define LED_PLAYPAUSE 3 // Keep at 3

// Default LED settings
#define NUM_PIXELS 5
#define DEFAULT_BRIGHTNESS 70

// Add new timing constants
#define BLINK_INTERVAL_MS 333      // For green blink (3 blinks in 1 second)
#define FADE_INTERVAL_MS 50        // For breathing effect
#define FLICKER_INTERVAL_MS 100    // For orange flicker

namespace LEDControl {
    class LedColors {
    private:
        static Adafruit_NeoPixel* pixelsPtr;

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
        
        // Add new animation methods
        static void startupAnimation();
        static void homingAnimation();
        static void readyBlinkAnimation();
        static void runningAnimation(bool flicker);
        static void pausedAnimation();
        
        // Add timing variables
        static unsigned long lastAnimationUpdate;
        static unsigned long flickerTimer;
    };
}
#endif
