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

namespace LEDControl {
    class LedColors {
    private:
        static Adafruit_NeoPixel* pixelsPtr;

    public:
        static uint32_t COLOR_IDLE;
        static uint32_t COLOR_PRESSED;
        static uint32_t COLOR_OFF;
        
        static uint32_t COLOR_RUNNING;
        static uint32_t COLOR_PAUSED;
        static uint32_t COLOR_COMPLETE;

        static uint32_t COLOR_ERROR;    // Add this for red error state
        static uint32_t COLOR_JOG;     // Add new color for jogging state
        
        static void init(Adafruit_NeoPixel& pixels);  // Declaration only
    };
}
#endif
