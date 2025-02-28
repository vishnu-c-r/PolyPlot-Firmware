#pragma once

#ifdef __cplusplus
#include <Adafruit_NeoPixel.h>

/**
 * LED Control namespace for CNC controller
 * Provides LED animations and state management for different machine states
 */
namespace LEDControl
{
    /**
     * LedColors class handles LED animations and color transitions
     * for different machine states (idle, running, paused, etc)
     */
    class LedColors
    {
    private:
        // Core references and internal state tracking
        static Adafruit_NeoPixel *pixelsPtr; // Reference to the NeoPixel object

        // Timing constants for animations
        static const unsigned long HOLD_TIME = 300;          // Time to hold solid colors (ms)
        static const unsigned long TRANSITION_INTERVAL = 15; // Delay between color transition steps (ms)

        /**
         * Interpolates between two colors with given percentage
         * @param c1 First color
         * @param c2 Second color
         * @param p Percentage (0-255) of interpolation
         * @return Interpolated color
         */
        static uint32_t interp(uint32_t c1, uint32_t c2, uint8_t p)
        {
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

        /**
         * Helper function to interpolate between two colors
         * @param color1 Starting color
         * @param color2 Target color
         * @param step Step value (0-255)
         * @return Interpolated color
         */
        static uint32_t interpolateColor(uint32_t color1, uint32_t color2, uint16_t step);

    public:
        // Add missing static member declaration
        static bool initAnimationComplete; // Tracks if startup animation has run

        // Machine states that correspond to different LED animations
        enum State
        {
            RUNNING,
            PAUSED,
            IDLE,
            JOGGING,
            HOMING,
            ALARM,
            COMPLETE
        };
        static State currentState; // Current machine state

        /**
         * Updates the current machine state
         * @param newState New state to set
         */
        static void updateMachineState(State newState)
        {
            currentState = newState;
        }

        //---------------------------------------------------------------
        // LED Hardware Configuration
        //---------------------------------------------------------------
        // LED positions on the NeoPixel strip
        static const uint8_t LED_UP = 0;              // Up arrow LED index
        static const uint8_t LED_RIGHT = 4;           // Right arrow LED index
        static const uint8_t LED_DOWN = 2;            // Down arrow LED index
        static const uint8_t LED_LEFT = 1;            // Left arrow LED index
        static const uint8_t LED_PLAYPAUSE = 3;       // Play/pause LED index
        static const uint8_t NUM_PIXELS = 5;          // Total number of LEDs
        static const uint8_t DEFAULT_BRIGHTNESS = 70; // Default LED brightness (0-255)

        //---------------------------------------------------------------
        // Animation Timing Constants
        //---------------------------------------------------------------
        static const uint16_t TRANSITION_DURATION = 300;   // Duration for state transitions (ms)
        static const uint16_t BLINK_INTERVAL_MS = 333;     // Blink speed for alarm state (ms)
        static const uint16_t FADE_INTERVAL_MS = 10;       // Speed of fade animation (ms)
        static const uint16_t HOMING_UPDATE_INTERVAL = 25; // Update rate for homing animation (ms)
        static const uint16_t FLICKER_INTERVAL_MS = 100;   // Flicker speed for running state (ms)
        static const uint8_t HOMING_TRANSITION_STEP = 8;   // Step size for homing transitions

        //---------------------------------------------------------------
        // Color Definitions
        //---------------------------------------------------------------
        static uint32_t COLOR_RED;    // Red color for alarm states
        static uint32_t COLOR_GREEN;  // Green color for idle state
        static uint32_t COLOR_ORANGE; // Orange color for running state
        static uint32_t COLOR_OFF;    // LEDs off
        // Animation colors
        static uint32_t Color1;    // Orange
        static uint32_t Color2;    // Magenta
        static uint32_t Color3;    // Cyan
        static uint32_t lastColor; // Tracks last displayed color for transitions

        //---------------------------------------------------------------
        // Animation State Variables
        //---------------------------------------------------------------
        static uint8_t blinkCount;                // Counts blinks in sequences
        static uint8_t fadeValue;                 // Current brightness in fade animations
        static int8_t fadeStep;                   // Direction and amount of fade steps
        static unsigned long lastAnimationUpdate; // Timing for animation updates
        static unsigned long flickerTimer;        // Timing for flicker effects
        static unsigned long previousMillis;      // Previous millis for time-based animations
        static uint16_t step;                     // Current step in animations

        // State flags
        static bool inHomingMode; // Whether we're in homing mode
        static bool isHomed;      // Whether homing is complete

        //---------------------------------------------------------------
        // Public Functions
        //---------------------------------------------------------------
        /**
         * Initialize the LED controller
         * @param pixels Reference to NeoPixel object
         */
        static void init(Adafruit_NeoPixel &pixels);

        /**
         * Animation for homing state - cycles through colors
         */
        static void homingAnimation();

        /**
         * Animation for running state - blinks center LED
         * @param flicker Whether to use flicker effect
         */
        static void runningAnimation(bool flicker);

        /**
         * Animation for paused state - fades center LED
         */
        static void pausedAnimation();

        /**
         * Animation for alarm state - blinks all LEDs red
         */
        static void alarmAnimation();

        /**
         * Transition from current color to green with blink
         * Used when moving to idle state
         */
        static void transitionToGreen();

        /**
         * Transition from current color to orange with flash
         * Used when starting operations
         */
        static void transitionToOrange();
    };
}
#endif
