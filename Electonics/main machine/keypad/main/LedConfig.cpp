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
        // Rainbow wheel excluding red/green
        static uint8_t wheelPos = 0;
        
        if (millis() - lastAnimationUpdate > 50) {
            lastAnimationUpdate = millis();
            wheelPos++;
            
            for (int i = 0; i < NUM_PIXELS; i++) {
                uint8_t pos = (wheelPos + (i * 256 / NUM_PIXELS)) & 255;
                
                // Skip red and green hues
                if (pos < 85) {
                    pixelsPtr->setPixelColor(i, pixelsPtr->Color(0, 0, pos * 3));
                } else if (pos < 170) {
                    pos -= 85;
                    pixelsPtr->setPixelColor(i, pixelsPtr->Color(pos * 3, 0, 255 - pos * 3));
                } else {
                    pos -= 170;
                    pixelsPtr->setPixelColor(i, pixelsPtr->Color(255 - pos * 3, 0, 0));
                }
            }
            pixelsPtr->show();
        }
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
        if (flicker && millis() - flickerTimer > FLICKER_INTERVAL) {
            flickerTimer = millis();
            uint8_t brightness = random(180, 255);
            pixelsPtr->setPixelColor(LED_PLAYPAUSE, pixelsPtr->Color(brightness, brightness/2, 0));
        }
        // Removed redundant pixelsPtr->show() for optimization.
    }

    void LedColors::pausedAnimation() {
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
}
