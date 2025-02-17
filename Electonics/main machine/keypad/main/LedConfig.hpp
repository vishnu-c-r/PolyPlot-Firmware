#pragma once

#ifdef __cplusplus
#include <Adafruit_NeoPixel.h>

namespace LEDControl {
    class LedColors {
    private:
        static Adafruit_NeoPixel* pixelsPtr;
        static unsigned long initStart;  // Add this declaration

        // Fix the interp function declaration and parameter
        static uint32_t interp(uint32_t c1, uint32_t c2, uint8_t p) {
            uint8_t r1 = (c1 >> 16) & 0xFF;
            uint8_t g1 = (c1 >> 8) & 0xFF;
            uint8_t b1 = c1 & 0xFF;
            
            uint8_t r2 = (c2 >> 16) & 0xFF;
            uint8_t g2 = (c2 >> 8) & 0xFF;
            uint8_t b2 = c2 & 0xFF;
            
            uint8_t r = r1 + ((r2 - r1) * p) / 255;
            uint8_t g = g1 + ((g2 - g1) * p) / 255;
            uint8_t b = b1 + ((b2 - b1) * p) / 255;
            
            return pixelsPtr->Color(r, g, b);
        }

    public:
        // LED positions as static constants
        static const uint8_t LED_UP = 0;
        static const uint8_t LED_RIGHT = 4;
        static const uint8_t LED_DOWN = 2;
        static const uint8_t LED_LEFT = 1;
        static const uint8_t LED_PLAYPAUSE = 3;
        static const uint8_t NUM_PIXELS = 5;
        static const uint8_t DEFAULT_BRIGHTNESS = 70;

        // Animation timing constants
        static const uint16_t TRANSITION_DURATION = 300;
        static const uint16_t BLINK_INTERVAL_MS = 333;
        static const uint16_t FADE_INTERVAL_MS = 10;    // Decreased from 20 to 10 for smoother fade
        static const uint16_t HOMING_UPDATE_INTERVAL = 25;  // Decreased from 30 to 15 (faster updates)
        static const uint16_t FLICKER_INTERVAL_MS = 100;  // Add this
        static const uint8_t HOMING_TRANSITION_STEP = 8;    // Increased from 2 to 8 (faster color transitions)

        // Add new timing constants for startup
        static const uint16_t STARTUP_FADE_INTERVAL = 10;  // 10ms for smooth fade
        static const uint8_t STARTUP_BRIGHTNESS_STEP = 2;  // Small steps for smooth fade

        // Colors and other state variables
        static uint32_t COLOR_RED;
        static uint32_t COLOR_GREEN;
        static uint32_t COLOR_ORANGE;
        static uint32_t COLOR_OFF;
        
        // Animation state
        static uint8_t blinkCount;
        static uint8_t fadeValue;
        static int8_t fadeStep;
        static unsigned long lastAnimationUpdate;
        static unsigned long flickerTimer;

        // Animation states
        static bool inStartupMode;      // Add this
        static bool inHomingMode;       // Add this

        // Function declarations
        static void init(Adafruit_NeoPixel& pixels);
        static void startupAnimation();
        static void homingAnimation();
        static void readyBlinkAnimation();
        static void runningAnimation(bool flicker);
        static void pausedAnimation();
        static void alarmAnimation();
        static void machineInitAnimation();
        static void transitionToReadyAnimation();
    };
}
#endif
