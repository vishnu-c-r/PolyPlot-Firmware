#include "LedConfig.hpp"

// Add definitions for the static color members
namespace LEDControl {
    uint32_t LedColors::COLOR_RED;
    uint32_t LedColors::COLOR_GREEN;
    uint32_t LedColors::COLOR_ORANGE;
    uint32_t LedColors::COLOR_OFF;
    
    // Initialize new static members
    bool LedColors::inStartupMode = true;
    bool LedColors::inHomingMode = false;
    uint8_t LedColors::blinkCount = 0;
    uint8_t LedColors::fadeValue = 0;
    int8_t LedColors::fadeStep = 5;
    unsigned long LedColors::lastAnimationUpdate = 0;
    unsigned long LedColors::flickerTimer = 0;
    unsigned long LedColors::initStart = 0;

    Adafruit_NeoPixel* LedColors::pixelsPtr = nullptr;

    void LedColors::init(Adafruit_NeoPixel& pixels) {
        pixelsPtr = &pixels;  // Store the pointer
        
        // Initialize new color scheme
        COLOR_RED = pixels.Color(255, 0, 0);      // Full red
        COLOR_GREEN = pixels.Color(0, 255, 0);     // Full green
        COLOR_ORANGE = pixels.Color(255, 100, 0);  // Bright orange
        COLOR_OFF = pixels.Color(0, 0, 0);         // Off
        
        // Clear LEDs on startup
        pixels.clear();
        pixels.show();
    }

    void LedColors::startupAnimation() {
        static unsigned long lastUpdate = 0;
        static float brightness = 0;
        static bool transitionToOrange = false;
        static uint32_t currentColor = COLOR_RED;
        
        if (millis() - lastUpdate > STARTUP_FADE_INTERVAL) {
            lastUpdate = millis();
            
            // Smoother sinusoidal breathing effect
            brightness = (sin(millis() * 0.001f) + 1.0f) * 127.5f;  // Range 0-255
            
            // After one full breath cycle, start transitioning to orange
            if (!transitionToOrange && millis() > 3000) {  // After 3 seconds
                transitionToOrange = true;
            }
            
            // If transitioning to orange, interpolate between red and orange
            if (transitionToOrange) {
                currentColor = interp(COLOR_RED, COLOR_ORANGE, brightness);
            }
            
            // Apply the breathing effect
            uint32_t finalColor;
            if (transitionToOrange) {
                finalColor = interp(COLOR_OFF, currentColor, brightness);
            } else {
                finalColor = interp(COLOR_OFF, COLOR_RED, brightness);
            }
            
            for (int i = 0; i < NUM_PIXELS; i++) {
                pixelsPtr->setPixelColor(i, finalColor);
            }
            
            pixelsPtr->show();
        }
    }

    void LedColors::homingAnimation() {
        static unsigned long lastUpdate = 0;
        if (millis() - lastUpdate < HOMING_UPDATE_INTERVAL) return;
        lastUpdate = millis();

        // Define custom colors
        static const uint32_t palette[] = {
            // pixelsPtr->Color(0, 255, 255),   // Cyan
            // pixelsPtr->Color(255, 0, 255),   // Magenta
            // pixelsPtr->Color(255, 255, 0),   // Yellow
            pixelsPtr->Color(255, 100, 0),   // Orange
            pixelsPtr->Color(255, 20, 147),  // Pink
            pixelsPtr->Color(0, 0, 200),     // Blue
            pixelsPtr->Color(255, 20, 147)   // Pink
        };
        static const uint8_t paletteSize = sizeof(palette) / sizeof(palette[0]);

        static uint8_t progress = 0;
        static int rotationOffset = 0;

        // Get the interpolated color between two palette colours
        int currentIndex = rotationOffset % paletteSize;
        int nextIndex = (rotationOffset + 1) % paletteSize;
        uint32_t interpColor = interp(palette[currentIndex], palette[nextIndex], progress);

        // Apply color to all LEDs
        for (int i = 0; i < NUM_PIXELS; i++) {
            pixelsPtr->setPixelColor(i, interpColor);
        }

        progress = min((uint16_t)255, (uint16_t)(progress + HOMING_TRANSITION_STEP));
        if (progress >= 255) {
            progress = 0;
            rotationOffset = (rotationOffset + 1) % paletteSize;
        }
        pixelsPtr->show();
    }

    void LedColors::readyBlinkAnimation() {
        static unsigned long lastBlink = 0;
        static bool blinkState = false;
        
        if (millis() - lastBlink > BLINK_INTERVAL_MS) {
            lastBlink = millis();
            blinkState = !blinkState;
            
            if (blinkState) {
                for (int i = 0; i < NUM_PIXELS; i++) {
                    // Use COLOR_GREEN instead of non-existent COLOR_COMPLETE
                    pixelsPtr->setPixelColor(i, COLOR_GREEN);
                }
            } else {
                for (int i = 0; i < NUM_PIXELS; i++) {
                    pixelsPtr->setPixelColor(i, COLOR_OFF);
                }
            }
            
            if (!blinkState) blinkCount++;
            pixelsPtr->show();
        }
    }

    void LedColors::runningAnimation(bool flicker) {
        static unsigned long lastBlink = 0;
        static bool ledState = false;

        if (millis() - lastBlink > FLICKER_INTERVAL_MS) {
            lastBlink = millis();
            ledState = !ledState;  // Toggle LED state

            // Turn off arrow keys
            pixelsPtr->setPixelColor(LED_UP, COLOR_OFF);
            pixelsPtr->setPixelColor(LED_RIGHT, COLOR_OFF);
            pixelsPtr->setPixelColor(LED_DOWN, COLOR_OFF);
            pixelsPtr->setPixelColor(LED_LEFT, COLOR_OFF);

            // Blink play/pause LED between orange and off
            pixelsPtr->setPixelColor(LED_PLAYPAUSE,
                                    ledState ? COLOR_ORANGE : COLOR_OFF);

            pixelsPtr->show();  // Make sure to show the changes
        }
    }

    void LedColors::pausedAnimation() {
        static unsigned long lastUpdate = 0;
        static uint8_t brightness = 0;
        static int8_t step = 2;  // Reduced from 5 to 2 for smoother transitions

        if (millis() - lastUpdate > FADE_INTERVAL_MS) {
            lastUpdate = millis();

            // Simple fade up/down
            brightness += step;
            if (brightness >= 255) {
                brightness = 255;  // Clamp at max
                step = -step;
            } else if (brightness <= 0) {
                brightness = 0;  // Clamp at min
                step = -step;
            }

            // Only affect play/pause LED with orange color
            pixelsPtr->setPixelColor(LED_PLAYPAUSE,
                                    pixelsPtr->Color(brightness, brightness / 2, 0));

            pixelsPtr->show();
        }
    }

    // New method: machine initialization animation.
    void LedColors::machineInitAnimation() {
        static unsigned long initStart = millis();
        unsigned long elapsed = millis() - initStart;
        if (elapsed < 2000) {
            // For first 2 sec, all LEDs solid red.
            for (uint8_t i = 0; i < NUM_PIXELS; i++) {
                pixelsPtr->setPixelColor(i, COLOR_RED);
            }
        } else if (elapsed < 5000) {
            // Next phase: wavy multicolor animation using hues except red and green.
            for (uint8_t i = 0; i < NUM_PIXELS; i++) {
                // Compute a pseudo-hue offset and use blue/purple tones.
                uint8_t phase = (millis() / 10 + i * 20) & 0xFF;
                // Skip red and green by keeping R low and G zero.
                uint8_t r = (phase % 2) ? 30 : 10;
                uint8_t b = 200 + ((phase % 56) - 28);
                pixelsPtr->setPixelColor(i, pixelsPtr->Color(r, 0, b));
            }
        } else {
            // Transition to ready state: smooth shift to green then three blinks.
            transitionToReadyAnimation();
        }
        pixelsPtr->show();
    }
    
    // New method: smoothly shift all LEDs to green then blink three times.
    void LedColors::transitionToReadyAnimation() {
        static unsigned long transStart = millis();
        static bool transitionDone = false;
        static uint8_t blinkCount = 0;
        unsigned long t = millis() - transStart;
        if (!transitionDone) {
            if(t < 1000) {
                // Interpolate from current color (assumed red) to green.
                uint8_t r = 255 - (uint8_t)(255 * t / 1000.0);
                uint8_t g = (uint8_t)(255 * t / 1000.0);
                for (uint8_t i = 0; i < NUM_PIXELS; i++) {
                    pixelsPtr->setPixelColor(i, pixelsPtr->Color(r, g, 0));
                }
            } else {
                // After transition, set all to green.
                for (uint8_t i = 0; i < NUM_PIXELS; i++) {
                    pixelsPtr->setPixelColor(i, COLOR_GREEN);
                }
                transitionDone = true;
                // Reset blink counter start.
                blinkCount = 0;
                transStart = millis();  // restart timer for blinking
            }
        } else {
            // Blink three times.
            static bool blinkOn = false;
            if (millis() - transStart > BLINK_INTERVAL_MS) {
                transStart = millis();
                blinkOn = !blinkOn;
                if (!blinkOn) {
                    blinkCount++;
                }
                for (uint8_t i = 0; i < NUM_PIXELS; i++) {
                    pixelsPtr->setPixelColor(i, blinkOn ? COLOR_GREEN : COLOR_OFF);
                }
            }
            if (blinkCount >= 3) {
                // End transition; reset variables for future use.
                transitionDone = false;
                initStart = millis();  // reuse initStart if needed elsewhere
            }
        }
        pixelsPtr->show();
    }
    
    void LedColors::alarmAnimation() {
        static unsigned long lastAlarm = 0;
        static bool alarmOn = false;
        if (millis() - lastAlarm > BLINK_INTERVAL_MS) {
            lastAlarm = millis();
            alarmOn = !alarmOn;
            for (uint8_t i = 0; i < NUM_PIXELS; i++) {
                pixelsPtr->setPixelColor(i, alarmOn ? COLOR_RED : COLOR_OFF);
            }
        }
    }
}
