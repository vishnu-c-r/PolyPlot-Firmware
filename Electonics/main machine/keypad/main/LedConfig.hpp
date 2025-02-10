#pragma once

#ifdef __cplusplus
#include <Adafruit_NeoPixel.h>

namespace LEDControl {
    class LedColors {
    private:
        static Adafruit_NeoPixel* pixelsPtr;
        static unsigned long initStart;  // Add this declaration

        // Add interpolation helper as a private static method
        static uint32_t interp(uint32_t c1, uint32_t c2, uint8_t p);

    public:
        //LED positions
        static const uint8_t LED_UP = 0;
        static const uint8_t LED_RIGHT = 4;
        static const uint8_t LED_DOWN = 2;
        static const uint8_t LED_LEFT = 1;
        static const uint8_t LED_PLAYPAUSE = 3;
        static const uint8_t NUM_PIXELS = 5;
        static const uint8_t DEFAULT_BRIGHTNESS = 70;

        // Colors
        static uint32_t COLOR_RED;
        static uint32_t COLOR_GREEN;
        static uint32_t COLOR_ORANGE;
        static uint32_t COLOR_OFF;

        // Animation timing constants
        static const uint16_t BLINK_INTERVAL_MS = 333;
        static const uint16_t FADE_INTERVAL_MS = 50;
        static const uint16_t HOMING_UPDATE_INTERVAL = 15;
        static const uint8_t HOMING_TRANSITION_STEP = 5;

        // Add missing timing constant
        static const uint16_t TRANSITION_DURATION = 300;
        static const uint16_t FLICKER_INTERVAL = 100;

        // Animation state
        static uint8_t blinkCount;
        static uint8_t fadeValue;
        static int8_t fadeStep;
        static unsigned long lastAnimationUpdate;
        static unsigned long flickerTimer;

        // Function declarations
        static void init(Adafruit_NeoPixel& pixels);
        static void homingAnimation();
        static void readyBlinkAnimation();
        static void runningAnimation(bool flicker);
        static void pausedAnimation();
        static void alarmAnimation();

        // Add missing function declarations
        static void startupAnimation();
        static void machineInitAnimation();
        static void transitionToReadyAnimation();
    };
}
#endif
