#include "LedConfig.hpp"
#include <math.h> // For sin(), PI

namespace LEDControl
{

    // ------------------------------------------------------------------------
    // Static Members Definitions
    // Colors for different machine states and animation types.
    uint32_t LedColors::COLOR_RED;
    uint32_t LedColors::COLOR_GREEN;
    uint32_t LedColors::COLOR_ORANGE;
    uint32_t LedColors::COLOR_OFF;

    // Flags and counters tracking animation states.
    bool LedColors::inStartupMode = true;
    bool LedColors::inHomingMode = false;
    uint8_t LedColors::blinkCount = 0;
    uint8_t LedColors::fadeValue = 0;
    int8_t LedColors::fadeStep = 5;
    unsigned long LedColors::lastAnimationUpdate = 0;
    unsigned long LedColors::flickerTimer = 0;

    // Pointer to the NeoPixel object; used to update LED colors.
    Adafruit_NeoPixel *LedColors::pixelsPtr = nullptr;

    // ------------------------------------------------------------------------
    // Initialization:
    // Sets up the NeoPixel array and assigns our color scheme.
    void LedColors::init(Adafruit_NeoPixel &pixels)
    {
        pixelsPtr = &pixels;
        COLOR_RED = pixels.Color(255, 0, 0);      // Used for startup/error animations.
        COLOR_GREEN = pixels.Color(0, 255, 0);    // Used for "ready" state.
        COLOR_ORANGE = pixels.Color(255, 100, 0); // Used for paused state.
        COLOR_OFF = pixels.Color(0, 0, 0);        // Off (black).
        pixels.clear();
        pixels.show();
    }

    // ------------------------------------------------------------------------
    // Helper function:
    // setAllPixels() applies the same color to every LED on the strip.
    static void setAllPixels(uint32_t color)
    {
        for (int i = 0; i < NUM_PIXELS; i++)
        {
            LedColors::getPixelsPtr()->setPixelColor(i, color);
        }
    }

    // ------------------------------------------------------------------------
    // startupAnimation():
    // Produces a "breathing" red effect by varying brightness via a sine function.
    // It uses the current state as the starting point, so transition is smooth.
    void LedColors::startupAnimation()
    {
        // Calculate a brightness value oscillating between 0 and 255.
        uint8_t brightness = (uint8_t)(127 + 127 * sin(millis() / 500.0));
        // Create a red color with the computed brightness.
        uint32_t color = getPixelsPtr()->Color(brightness, 0, 0);
        for (int i = 0; i < NUM_PIXELS; i++)
        {
            // Set each LED to the red color.
            getPixelsPtr()->setPixelColor(i, color);
        }
        getPixelsPtr()->show();
    }

    // ------------------------------------------------------------------------
    // homingAnimation():
    // Transitions from the startup red color to a sequence of custom colors
    // (Orange, Blue, Yellow, Purple) using a linear interpolation.
    // The interpolation factor increases gradually over time based on
    // homingInterpolationDuration.
    void LedColors::homingAnimation()
    {
        // Define the palette of target colors for homing.
        static const uint32_t homingPaletteColors[] = {
            getPixelsPtr()->Color(255, 165, 0), // Orange
            getPixelsPtr()->Color(0, 0, 255),   // Blue
            getPixelsPtr()->Color(255, 255, 0), // Yellow
            getPixelsPtr()->Color(128, 0, 128)  // Purple
        };
        static const uint8_t homingPaletteSize = sizeof(homingPaletteColors) / sizeof(homingPaletteColors[0]);
        // currentHomingIndex selects which color in the palette is the target.
        static uint8_t currentHomingIndex = 0;
        // homingInterpFactor ranges from 0.0 to 1.0 during the transition.
        static float homingInterpFactor = 0.0;
        // homingLastUpdate tracks the last time (in ms) the animation updated.
        static unsigned long homingLastUpdate = millis();

        // Use the current state as the starting color.
        // Here we assume COLOR_RED is the startup color.
        uint32_t startColor = COLOR_RED;
        uint32_t targetColor = homingPaletteColors[currentHomingIndex];

        // dt: Time elapsed since last update in seconds.
        float dt = (millis() - homingLastUpdate) / 1000.0;
        homingLastUpdate = millis();
        // Increase the interpolation factor proportionally to dt.
        homingInterpFactor += dt / homingInterpolationDuration;
        if (homingInterpFactor > 1.0)
            homingInterpFactor = 1.0;

        // Decompose start and target colors into RGB components.
        uint8_t sr = (startColor >> 16) & 0xFF;
        uint8_t sg = (startColor >> 8) & 0xFF;
        uint8_t sb = startColor & 0xFF;
        uint8_t tr = (targetColor >> 16) & 0xFF;
        uint8_t tg = (targetColor >> 8) & 0xFF;
        uint8_t tb = targetColor & 0xFF;

        // Compute the interpolated color using linear interpolation.
        uint8_t r = sr * (1.0 - homingInterpFactor) + tr * homingInterpFactor;
        uint8_t g = sg * (1.0 - homingInterpFactor) + tg * homingInterpFactor;
        uint8_t b = sb * (1.0 - homingInterpFactor) + tb * homingInterpFactor;
        uint32_t color = getPixelsPtr()->Color(r, g, b);
        setAllPixels(color); // Update all LEDs.
        getPixelsPtr()->show();

        // When the interpolation is complete (factor = 1.0) and after a brief timing check,
        // advance to the next target color in the palette.
        if (homingInterpFactor >= 1.0 && (millis() % 2000) < 50 && currentHomingIndex < homingPaletteSize - 1)
        {
            currentHomingIndex++;
            homingInterpFactor = 0.0; // Reset for next transition.
        }
    }

    // ------------------------------------------------------------------------
    // pausedAnimation():
    // Creates a fade in and fade out effect on the play/pause LED using a sine
    // function. The rise and fall durations are adjustable via pausedRiseTime
    // and pausedFallTime.
    void LedColors::pausedAnimation()
    {
        // Calculate total period for one full cycle.
        unsigned long period = pausedRiseTime + pausedFallTime;
        // t: current time offset within the period.
        unsigned long t = millis() % period;
        float brightnessFactor;
        if (t < pausedRiseTime)
        {
            // During the rise phase, brightness increases smoothly.
            brightnessFactor = sin((t / (float)pausedRiseTime) * (PI / 2));
        }
        else
        {
            // During the fall phase, brightness decreases smoothly.
            brightnessFactor = sin(((pausedFallTime - (t - pausedRiseTime)) / (float)pausedFallTime) * (PI / 2));
        }
        // Base orange color for play/pause.
        uint8_t baseR = 255, baseG = 100, baseB = 0;
        uint8_t r = (uint8_t)(baseR * brightnessFactor);
        uint8_t g = (uint8_t)(baseG * brightnessFactor);
        uint8_t b = (uint8_t)(baseB * brightnessFactor);
        // Apply computed orange color to the play/pause LED.
        getPixelsPtr()->setPixelColor(LED_PLAYPAUSE, getPixelsPtr()->Color(r, g, b));
        // Note: The show() call is assumed to be invoked by the caller after animation update.
    }

    // ------------------------------------------------------------------------
    // machineInitAnimation():
    // Chooses either startupAnimation or homingAnimation depending on the
    // state of inHomingMode.
    void LedColors::machineInitAnimation()
    {
        if (!inHomingMode)
        {
            startupAnimation();
        }
        else
        {
            homingAnimation();
        }
    }

    // ------------------------------------------------------------------------
    // transitionToReadyAnimation():
    // Smoothly fades the LEDs to green over one second.
    void LedColors::transitionToReadyAnimation()
    {
        static unsigned long transStart = millis();
        unsigned long t = millis() - transStart;
        if (t < 1000)
        {
            // Linearly interpolate from current color towards green.
            uint8_t r = 255 - (uint8_t)(255 * t / 1000.0);
            uint8_t g = (uint8_t)(255 * t / 1000.0);
            for (uint8_t i = 0; i < NUM_PIXELS; i++)
            {
                getPixelsPtr()->setPixelColor(i, getPixelsPtr()->Color(r, g, 0));
            }
        }
        else
        {
            setAllPixels(COLOR_GREEN);
        }
        getPixelsPtr()->show();
    }

    // ------------------------------------------------------------------------
    // readyBlinkAnimation():
    // Toggles all LEDs between green and off at a fixed blink interval.
    void LedColors::readyBlinkAnimation()
    {
        static unsigned long lastBlink = 0;
        static bool blinkState = false;
        if (millis() - lastBlink > BLINK_INTERVAL_MS)
        {
            lastBlink = millis();
            blinkState = !blinkState;
            if (blinkState)
            {
                setAllPixels(COLOR_GREEN);
            }
            else
            {
                setAllPixels(COLOR_OFF);
            }
            if (!blinkState)
                blinkCount++; // Increment blink counter when turning off.
            getPixelsPtr()->show();
        }
    }

    // ------------------------------------------------------------------------
    // runningAnimation():
    // In running mode, directional LEDs are turned off. If flicker is enabled,
    // the play/pause LED is randomly updated for a flicker effect.
    void LedColors::runningAnimation(bool flicker)
    {
        // Turn off directional (arrow) LEDs.
        getPixelsPtr()->setPixelColor(LED_UP, COLOR_OFF);
        getPixelsPtr()->setPixelColor(LED_RIGHT, COLOR_OFF);
        getPixelsPtr()->setPixelColor(LED_DOWN, COLOR_OFF);
        getPixelsPtr()->setPixelColor(LED_LEFT, COLOR_OFF);
        // If flickering is required and enough time has passed:
        if (flicker && (millis() - flickerTimer > FLICKER_INTERVAL_MS))
        {
            flickerTimer = millis();
            uint8_t brightness = random(180, 255);
            getPixelsPtr()->setPixelColor(LED_PLAYPAUSE, getPixelsPtr()->Color(brightness, brightness / 2, 0));
        }
    }

    // ------------------------------------------------------------------------
    // alarmAnimation():
    // Blinks all LEDs red at a fixed interval to indicate an alarm.
    void LedColors::alarmAnimation()
    {
        static unsigned long lastAlarm = 0;
        static bool alarmOn = false;
        if (millis() - lastAlarm > BLINK_INTERVAL_MS)
        {
            lastAlarm = millis();
            alarmOn = !alarmOn;
            setAllPixels(alarmOn ? COLOR_RED : COLOR_OFF);
        }
        // Caller is responsible for invoking show().
    }
}
