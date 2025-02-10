#include "LedConfig.hpp"

namespace LEDControl {
    // Color definitions
    uint32_t LedColors::COLOR_RED;
    uint32_t LedColors::COLOR_GREEN;
    uint32_t LedColors::COLOR_ORANGE;
    uint32_t LedColors::COLOR_OFF;
    
    // Animation state variables
    uint8_t LedColors::blinkCount = 0;
    uint8_t LedColors::fadeValue = 0;
    int8_t LedColors::fadeStep = 5;
    unsigned long LedColors::lastAnimationUpdate = 0;
    unsigned long LedColors::flickerTimer = 0;
    unsigned long LedColors::initStart = 0;  // Add this definition

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
        // All LEDs solid red until homing starts
        for (int i = 0; i < NUM_PIXELS; i++) {
            pixelsPtr->setPixelColor(i, COLOR_RED);
        }
        pixelsPtr->show();
    }

    void LedColors::homingAnimation() {
        static unsigned long lastUpdate = 0;
        if (millis() - lastUpdate < HOMING_UPDATE_INTERVAL) return;
        lastUpdate = millis();
        
        static uint32_t palette[4];
        static bool paletteInitialized = false;
        if (!paletteInitialized) {
            palette[0] = pixelsPtr->Color(0, 255, 255);   // Cyan
            palette[1] = pixelsPtr->Color(255, 0, 255);   // Magenta
            palette[2] = pixelsPtr->Color(255, 160, 0);   // Orange
            palette[3] = pixelsPtr->Color(128, 0, 128);   // Purple
            paletteInitialized = true;
        }
        
        static uint8_t progress = 0;
        static int rotationOffset = 0;
    
        // Get the interpolated color between two palette colours
        int currentIndex = rotationOffset;
        int nextIndex = (rotationOffset + 1) % 4;
        uint32_t interpColor = interp(palette[currentIndex], palette[nextIndex], progress);
    
        // Remove breathing effect and just use the interpolated color directly
        for (int i = 0; i < NUM_PIXELS; i++) {
            pixelsPtr->setPixelColor(i, interpColor);
        }
    
        progress = min((uint16_t)255, (uint16_t)(progress + HOMING_TRANSITION_STEP));
        if (progress >= 255) {
            progress = 0;
            rotationOffset = (rotationOffset + 1) % 4;
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
        // Turn off arrow keys.
        pixelsPtr->setPixelColor(LED_UP, COLOR_OFF);
        pixelsPtr->setPixelColor(LED_RIGHT, COLOR_OFF);
        pixelsPtr->setPixelColor(LED_DOWN, COLOR_OFF);
        pixelsPtr->setPixelColor(LED_LEFT, COLOR_OFF);
        // Flicker Play/Pause LED.
        if (flicker && (millis() - flickerTimer > FLICKER_INTERVAL)) {
            flickerTimer = millis();
            uint8_t brightness = random(180, 255);
            pixelsPtr->setPixelColor(LED_PLAYPAUSE, pixelsPtr->Color(brightness, brightness / 2, 0));
        }
        // Removed redundant pixelsPtr->show() for optimization.
    }

    void LedColors::pausedAnimation() {
        // Ensure arrow keys remain off.
        pixelsPtr->setPixelColor(LED_UP, COLOR_OFF);
        pixelsPtr->setPixelColor(LED_RIGHT, COLOR_OFF);
        pixelsPtr->setPixelColor(LED_DOWN, COLOR_OFF);
        pixelsPtr->setPixelColor(LED_LEFT, COLOR_OFF);
        if (millis() - lastAnimationUpdate > FADE_INTERVAL_MS) {
            lastAnimationUpdate = millis();
            fadeValue += fadeStep;
            if (fadeValue >= 255 || fadeValue <= 0) {
                fadeStep = -fadeStep;
            }
            uint32_t color = pixelsPtr->Color(
                map(fadeValue, 0, 255, 0, 255),
                map(fadeValue, 0, 255, 0, 100),
                0
            );
            pixelsPtr->setPixelColor(LED_PLAYPAUSE, color);
        }
        // Removed immediate show() call.
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
                transStart = millis(); // restart timer for blinking
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
        // Note: pixelsPtr->show() is called in main.ino after animation.
    }

    // Add interpolation method implementation
    uint32_t LedColors::interp(uint32_t c1, uint32_t c2, uint8_t p) {
        auto getR = [](uint32_t col) -> uint8_t { return (col >> 16) & 0xFF; };
        auto getG = [](uint32_t col) -> uint8_t { return (col >> 8) & 0xFF; };
        auto getB = [](uint32_t col) -> uint8_t { return col & 0xFF; };
        uint8_t r = getR(c1) + ((getR(c2) - getR(c1)) * p) / 255;
        uint8_t g = getG(c1) + ((getG(c2) - getG(c1)) * p) / 255;
        uint8_t b = getB(c1) + ((getB(c2) - getB(c1)) * p) / 255;
        return pixelsPtr->Color(r, g, b);
    }
}
