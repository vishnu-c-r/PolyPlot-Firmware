/**
 * LED Controller Implementation
 *
 * Handles LED animations and transitions for the CNC controller
 * Provides visual feedback for different machine states
 */
#include "LedConfig.hpp"
#include "main.h"

namespace LEDControl
{
    //---------------------------------------------------------------
    //              Static Member Initialization
    //---------------------------------------------------------------
    // Color definitions - initialized in init()
    uint32_t LedColors::COLOR_RED;
    uint32_t LedColors::COLOR_GREEN;
    uint32_t LedColors::COLOR_ORANGE;
    uint32_t LedColors::COLOR_OFF;
    uint32_t LedColors::Color1;
    uint32_t LedColors::Color2;
    uint32_t LedColors::Color3;
    uint32_t LedColors::lastColor;

    // Animation state variables
    // bool LedColors::inHomingMode = false; 
    uint8_t LedColors::blinkCount = 0;
    uint8_t LedColors::fadeValue = 0;
    int8_t LedColors::fadeStep = 5;
    unsigned long LedColors::lastAnimationUpdate = 0;
    unsigned long LedColors::flickerTimer = 0;
    unsigned long LedColors::previousMillis = 0;
    bool LedColors::isHomed = false;
    uint16_t LedColors::step = 0;

    // State tracking
    LedColors::State LedColors::currentState = IDLE;
    bool LedColors::initAnimationComplete = false;

    // NeoPixel reference
    Adafruit_NeoPixel *LedColors::pixelsPtr = nullptr;

    /**
     * Interpolate between two colors by given step value
     * Used for smooth transitions between colors
     */
    uint32_t LedColors::interpolateColor(uint32_t color1, uint32_t color2, uint16_t step)
    {
        // This function duplicates functionality with interp(),
        // but is used throughout the code so keep it
        // Extract color components
        uint8_t r1 = (color1 >> 16) & 0xFF;
        uint8_t g1 = (color1 >> 8) & 0xFF;
        uint8_t b1 = color1 & 0xFF;

        uint8_t r2 = (color2 >> 16) & 0xFF;
        uint8_t g2 = (color2 >> 8) & 0xFF;
        uint8_t b2 = color2 & 0xFF;

        // Linear interpolation of each component
        uint8_t r = r1 + ((r2 - r1) * step) / 255;
        uint8_t g = g1 + ((g2 - g1) * step) / 255;
        uint8_t b = b1 + ((b2 - b1) * step) / 255;

        return pixelsPtr->Color(r, g, b);
    }

    /**
     * Initialize LED system and play startup animation
     */
    void LedColors::init(Adafruit_NeoPixel &pixels)
    {
        // Store reference to NeoPixel object
        pixelsPtr = &pixels;

        // Initialize NeoPixel library
        pixels.begin();
        pixels.setBrightness(DEFAULT_BRIGHTNESS);
        pixels.clear();
        pixels.show();

        // Define standard colors
        COLOR_RED = pixels.Color(255, 0, 0);      // Full red
        COLOR_GREEN = pixels.Color(0, 255, 0);    // Full green
        COLOR_ORANGE = pixels.Color(255, 100, 0); // Bright orange
        COLOR_OFF = pixels.Color(0, 0, 0);        // Off

        // Define animation colors
        Color1 = pixels.Color(255, 165, 0); // Orange
        Color2 = pixels.Color(255, 0, 255); // Magenta
        Color3 = pixels.Color(0, 255, 255); // Cyan

        // Run startup animation only once
        if (!initAnimationComplete)
        {
            // Phase 1: Fade from black to red
            for (int j = 0; j <= 255; j += 3)
            {
                uint32_t interpolatedColor = interpolateColor(COLOR_OFF, COLOR_RED, j);
                for (int i = 0; i < NUM_PIXELS; i++)
                {
                    pixelsPtr->setPixelColor(i, interpolatedColor);
                }
                pixelsPtr->show();
                delay(TRANSITION_INTERVAL);
            }
            delay(HOLD_TIME); // Hold red briefly

            // // Phase 2: Transition from red to orange
            // for (int j = 0; j <= 255; j += 3)
            // {
            //     uint32_t interpolatedColor = interpolateColor(COLOR_RED, Color1, j);
            //     for (int i = 0; i < NUM_PIXELS; i++)
            //     {
            //         pixelsPtr->setPixelColor(i, interpolatedColor);
            //     }
            //     pixelsPtr->show();
            //     delay(TRANSITION_INTERVAL);
            // }
            // delay(HOLD_TIME); // Hold orange briefly

            // // Set first homing color to ensure smooth transition to homing animation
            // for (int i = 0; i < NUM_PIXELS; i++)
            // {
            //     pixelsPtr->setPixelColor(i, Color1);
            // }
            // pixelsPtr->show();
            // lastColor = Color1;

            initAnimationComplete = true; // Mark as complete
        }
        // Don't clear LEDs - let homing animation take over seamlessly
    }

    /**
     * Animation displayed during homing operations
     * Cycles through colors in a smoothly transitioning pattern
     */
    void LedColors::homingAnimation()
    {
        // Static variables maintain state between calls
        static uint8_t currentColor = 1;     // Current color in the cycle (1-3)
        static bool isTransitioning = false; // Whether we're between colors
        static uint16_t transitionStep = 0;  // Current step in transition (0-255)

        // Don't run if already homed
        if (isHomed)
            return;

        // Time-based animation
        unsigned long currentMillis = millis();
        if (currentMillis - previousMillis >= TRANSITION_INTERVAL)
        {
            previousMillis = currentMillis;

            if (isTransitioning)
            {
                // Transitioning between colors
                uint32_t fromColor, toColor;

                // Select source and target colors based on current position
                switch (currentColor)
                {
                case 1: // Orange to Magenta
                    fromColor = Color1;
                    toColor = Color2;
                    break;
                case 2: // Magenta to Cyan
                    fromColor = Color2;
                    toColor = Color3;
                    break;
                default: // Cyan back to Orange
                    fromColor = Color3;
                    toColor = Color1;
                    break;
                }

                // Calculate intermediate color and update all LEDs
                uint32_t interpolatedColor = interpolateColor(fromColor, toColor, transitionStep);
                for (int i = 0; i < NUM_PIXELS; i++)
                {
                    pixelsPtr->setPixelColor(i, interpolatedColor);
                }
                pixelsPtr->show();

                // Increment step and check for completion
                transitionStep += 3;
                if (transitionStep > 255)
                {
                    // Transition complete, move to next solid color
                    transitionStep = 0;
                    isTransitioning = false;
                    currentColor = (currentColor >= 3) ? 1 : currentColor + 1;
                    delay(HOLD_TIME); // Hold at target color
                }
            }
            else
            {
                // Display solid color before starting next transition
                uint32_t solidColor;
                switch (currentColor)
                {
                case 1:
                    solidColor = Color1;
                    break; // Orange
                case 2:
                    solidColor = Color2;
                    break; // Magenta
                default:
                    solidColor = Color3;
                    break; // Cyan
                }

                // Set all LEDs to solid color
                for (int i = 0; i < NUM_PIXELS; i++)
                {
                    pixelsPtr->setPixelColor(i, solidColor);
                }
                pixelsPtr->show();
                isTransitioning = true; // Begin transition on next update
            }

            // Store current color for future transitions
            lastColor = pixelsPtr->getPixelColor(0);
        }
    }

    /**
     * Animation for running state
     * Blinks the center play/pause LED in orange
     */
    void LedColors::runningAnimation(bool flicker)
    {
        static unsigned long lastBlink = 0;
        static bool ledState = false;

        // Time-based blinking
        if (millis() - lastBlink > FLICKER_INTERVAL_MS)
        {
            lastBlink = millis();
            ledState = !ledState; // Toggle state

            // Ensure directional LEDs are off
            pixelsPtr->setPixelColor(LED_UP, COLOR_OFF);
            pixelsPtr->setPixelColor(LED_RIGHT, COLOR_OFF);
            pixelsPtr->setPixelColor(LED_DOWN, COLOR_OFF);
            pixelsPtr->setPixelColor(LED_LEFT, COLOR_OFF);

            // Blink center LED
            pixelsPtr->setPixelColor(LED_PLAYPAUSE, ledState ? COLOR_ORANGE : COLOR_OFF);
            pixelsPtr->show();
        }
    }

    /**
     * Animation for paused state
     * Fades the center play/pause LED up and down
     */
    void LedColors::pausedAnimation()
    {
        static unsigned long lastUpdate = 0;
        static uint8_t brightness = 0;
        static int8_t step = 2; // Step size for fade

        // Time-based fading
        if (millis() - lastUpdate > FADE_INTERVAL_MS)
        {
            lastUpdate = millis();

            // Update brightness with direction
            brightness += step;

            // Reverse direction at limits
            if (brightness >= 255)
            {
                brightness = 255;
                step = -step;
            }
            else if (brightness <= 0)
            {
                brightness = 0;
                step = -step;
            }

            // Set center LED to orange with current brightness
            pixelsPtr->setPixelColor(LED_PLAYPAUSE,
                                    pixelsPtr->Color(brightness, brightness / 2, 0));
            pixelsPtr->show();
        }
    }

    /**
     * Animation for alarm state
     * Blinks all LEDs red
     */
    void LedColors::alarmAnimation()
    {
        static unsigned long lastAlarm = 0;
        static bool alarmOn = false;

        // Time-based blinking
        if (millis() - lastAlarm > BLINK_INTERVAL_MS)
        {
            lastAlarm = millis();
            alarmOn = !alarmOn; // Toggle state

            // Set all LEDs to either red or off
            for (uint8_t i = 0; i < NUM_PIXELS; i++)
            {
                pixelsPtr->setPixelColor(i, alarmOn ? COLOR_RED : COLOR_OFF);
            }
            pixelsPtr->show();
        }
    }

    /**
     * Transition from current color to green
     * Used when machine returns to idle state
     */
    void LedColors::transitionToGreen()
    {
        // Set homed flag to prevent re-entry and stop homing animation
        isHomed = true;

        // Phase 1: Smooth transition to green
        for (int j = 0; j <= 255; j++)
        {
            uint32_t interpolatedColor = interpolateColor(lastColor, COLOR_GREEN, j);
            for (int i = 0; i < NUM_PIXELS; i++)
            {
                pixelsPtr->setPixelColor(i, interpolatedColor);
            }
            pixelsPtr->show();
            delay(2); // Fast but smooth transition
        }

        // Pause at green before blinking
        delay(800);

        // Phase 2: Blink green 3 times to confirm completion
        for (int blinkCount = 0; blinkCount < 3; blinkCount++)
        {
            // Off phase
            for (int i = 0; i < NUM_PIXELS; i++)
            {
                pixelsPtr->setPixelColor(i, COLOR_OFF);
            }
            pixelsPtr->show();
            delay(200);

            // On phase
            for (int i = 0; i < NUM_PIXELS; i++)
            {
                pixelsPtr->setPixelColor(i, COLOR_GREEN);
            }
            pixelsPtr->show();
            delay(200);
        }
    }

    /**
     * Transition from current color to orange
     * Used when machine starts operations
     */
    void LedColors::transitionToOrange()
    {
        // Capture current LED colors for smooth transition
        uint32_t currentColors[NUM_PIXELS];
        for (int i = 0; i < NUM_PIXELS; i++)
        {
            currentColors[i] = pixelsPtr->getPixelColor(i);
        }

        // Phase 1: Smooth transition to orange
        for (int j = 0; j <= 255; j += 3)
        {
            for (int i = 0; i < NUM_PIXELS; i++)
            {
                uint32_t interpolatedColor = interpolateColor(currentColors[i], COLOR_ORANGE, j);
                pixelsPtr->setPixelColor(i, interpolatedColor);
            }
            pixelsPtr->show();
            delay(2);
        }

        // Brief pause at orange
        delay(200);

        // Phase 2: Flash orange twice to indicate action
        for (int flash = 0; flash < 2; flash++)
        {
            // Off phase
            for (int i = 0; i < NUM_PIXELS; i++)
            {
                pixelsPtr->setPixelColor(i, COLOR_OFF);
            }
            pixelsPtr->show();
            delay(150);

            // On phase
            for (int i = 0; i < NUM_PIXELS; i++)
            {
                pixelsPtr->setPixelColor(i, COLOR_ORANGE);
            }
            pixelsPtr->show();
            delay(150);
        }
    }

    /**
     * Breathing red animation for initial startup
     * Creates a pulsing red effect while waiting for machine to be ready
     */
    void LedColors::breathingRedAnimation()
    {
        // Static variables for animation state
        static unsigned long lastUpdate = 0;
        static uint8_t brightness = 255; // Start from FULL brightness (255)
        static int8_t step = -2; // Faster initial fade-down step
        static bool initialized = false;
        static bool initialFadeDone = false; // Tracks completion of initial fade to DEFAULT_BRIGHTNESS
        
        // Initialize on first call
        if (!initialized) {
            // Set all LEDs to red with FULL brightness
            uint32_t startRed = pixelsPtr->Color(255, 0, 0);
            for (int i = 0; i < NUM_PIXELS; i++) {
                pixelsPtr->setPixelColor(i, startRed);
            }
            pixelsPtr->show();
            lastColor = startRed;
            initialized = true;
        }
        
        // Time-based breathing effect
        unsigned long currentMillis = millis();
        if (currentMillis - lastUpdate >= 10) { // Update every 10ms for smooth breathing
            lastUpdate = currentMillis;

            // First phase: Initial fade from full brightness to DEFAULT_BRIGHTNESS
            if (!initialFadeDone) {
                // Update brightness with faster step for initial fade
                brightness += step;
                
                // When we reach DEFAULT_BRIGHTNESS, switch to regular breathing
                if (brightness <= DEFAULT_BRIGHTNESS) {
                    brightness = DEFAULT_BRIGHTNESS;
                    initialFadeDone = true;
                    step = -1; // Switch to regular step size for breathing
                }
            }
            // Second phase: Regular breathing animation between DEFAULT_BRIGHTNESS and minimum
            else {
                // Update brightness with direction
                brightness += step;
                
                // Reverse direction at limits with some delay at max/min
                if (brightness >= DEFAULT_BRIGHTNESS) { // Cap maximum brightness at DEFAULT_BRIGHTNESS
                    brightness = DEFAULT_BRIGHTNESS;
                    step = -step;
                } else if (brightness <= 5) { // Keep a minimum glow
                    brightness = 5;
                    step = -step;
                }
            }

            // Apply brightness to create the red glow
            uint32_t dimRed = pixelsPtr->Color(brightness, 0, 0);
            
            // Apply to all LEDs
            for (int i = 0; i < NUM_PIXELS; i++) {
                pixelsPtr->setPixelColor(i, dimRed);
            }
            pixelsPtr->show();
            
            // Store last color for future transitions
            lastColor = dimRed;
        }
    }

    /**
     * Transition from breathing red to homing animation start color
     * Creates a smooth transition from current red to Color1 (orange)
     */
    void LedColors::transitionToHoming()
    {
        // Capture current red color (from breathing animation)
        uint32_t startColor = lastColor;
        
        // Smoothly transition to Color1 (orange)
        for (int j = 0; j <= 255; j += 3)
        {
            uint32_t interpolatedColor = interpolateColor(startColor, Color1, j);
            for (int i = 0; i < NUM_PIXELS; i++)
            {
                pixelsPtr->setPixelColor(i, interpolatedColor);
            }
            pixelsPtr->show();
            delay(2); // Fast but smooth transition
        }
        
        // Brief flash to indicate homing is starting
        for (int i = 0; i < NUM_PIXELS; i++)
        {
            pixelsPtr->setPixelColor(i, COLOR_OFF);
        }
        pixelsPtr->show();
        delay(100);
        
        // Set all LEDs to Color1 (first color of homing sequence)
        for (int i = 0; i < NUM_PIXELS; i++)
        {
            pixelsPtr->setPixelColor(i, Color1);
        }
        pixelsPtr->show();
        
        // Make sure lastColor is updated
        lastColor = Color1;
    }
