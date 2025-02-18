#pragma once

#ifdef __cplusplus
#include <Adafruit_NeoPixel.h>

// LED ordering, count and brightness
static const uint8_t LED_UP = 0;
static const uint8_t LED_RIGHT = 4;
static const uint8_t LED_DOWN = 2;
static const uint8_t LED_LEFT = 1;
static const uint8_t LED_PLAYPAUSE = 3;
static const uint8_t DEFAULT_BRIGHTNESS = 70;
static const uint8_t NUM_PIXELS = 5;  // Added definition for NUM_PIXELS

// Timing constants for various animations
constexpr uint16_t BLINK_INTERVAL_MS = 333;   // For green blink (3 blinks in 1 second)
constexpr uint16_t FADE_INTERVAL_MS = 50;     // For breathing effect
constexpr uint16_t FLICKER_INTERVAL_MS = 100; // For orange flicker

namespace LEDControl
{
    class LedColors
    {
    private:
        static Adafruit_NeoPixel *pixelsPtr;

    public:
        // LED scheme colors
        static uint32_t COLOR_RED;    // For startup and errors
        static uint32_t COLOR_GREEN;  // For ready state
        static uint32_t COLOR_ORANGE; // For paused state
        static uint32_t COLOR_OFF;    // For inactive LEDs

        // Animation states
        static bool inStartupMode;
        static bool inHomingMode;
        static uint8_t blinkCount;
        static uint8_t fadeValue;
        static int8_t fadeStep;
        static unsigned long lastAnimationUpdate;
        static unsigned long flickerTimer;

        // Animation method declarations
        static void init(Adafruit_NeoPixel &pixels);
        static void startupAnimation();
        static void homingAnimation();
        static void readyBlinkAnimation();
        static void runningAnimation(bool flicker);
        static void pausedAnimation();
        static void alarmAnimation();
        static void machineInitAnimation();
        static void transitionToReadyAnimation();

        // Adjustable animation parameters for easier tweaking:
        inline static float homingInterpolationDuration = 2.0;   // seconds for homing interpolation
        inline static const unsigned long pausedRiseTime = 2000; // ms for paused animation rise phase
        inline static const unsigned long pausedFallTime = 1000; // ms for paused animation fall phase

        // Public accessor for the private pointer.
        static Adafruit_NeoPixel *getPixelsPtr() { return pixelsPtr; }
    };
}
#endif
