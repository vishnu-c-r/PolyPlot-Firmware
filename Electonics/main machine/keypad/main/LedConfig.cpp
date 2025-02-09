#include "LedConfig.hpp"

namespace LEDControl {
    // Initialize static color definitions
    uint32_t LedColors::COLOR_RED;     // Error/alarm indication
    uint32_t LedColors::COLOR_GREEN;   // Ready/success indication  
    uint32_t LedColors::COLOR_ORANGE;  // Active/running indication
    uint32_t LedColors::COLOR_OFF;     // LED disabled

    // Animation state tracking
    bool LedColors::inHomingMode = false;
    uint8_t LedColors::blinkCount = 0;
    uint8_t LedColors::fadeValue = 0;
    int8_t LedColors::fadeStep = 2;    // Controls breathing animation smoothness
    
    // Animation timing
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

    void LedColors::homingAnimation() {
        static unsigned long lastUpdate = 0;
        if (millis() - lastUpdate < HOMING_UPDATE_INTERVAL) return;
        lastUpdate = millis();
        
        // Smooth startup transition
        static bool fadeInComplete = false;
        static uint8_t fadeInIntensity = 0;
        
        // Handle initial fade-in
        if (!fadeInComplete) {
            fadeInIntensity += 5; // Increase gradually
            if (fadeInIntensity >= 255) { 
                fadeInIntensity = 255; 
                fadeInComplete = true; 
            }
            // Fade in using the first palette color (Cyan) scaled by fadeInIntensity
            uint32_t baseColor = pixelsPtr->Color(0 * fadeInIntensity / 255,
                                                   255 * fadeInIntensity / 255,
                                                   255 * fadeInIntensity / 255);
            for (int i = 0; i < NUM_PIXELS; i++) {
                pixelsPtr->setPixelColor(i, baseColor);
            }
            pixelsPtr->show();
            return;
        }
        
        // Main homing animation with color cycling
        static uint32_t palette[4];  // Color sequence for animation
        static bool paletteInitialized = false;
        if (!paletteInitialized) {
            // Define animation color sequence
            palette[0] = pixelsPtr->Color(0, 255, 255);   // Cyan
            palette[1] = pixelsPtr->Color(255, 0, 255);   // Magenta
            palette[2] = pixelsPtr->Color(255, 160, 0);   // Orange
            palette[3] = pixelsPtr->Color(128, 0, 128);   // Purple
            // palette[4] = pixelsPtr->Color(255, 0, 0);     // Red
            // palette[5] = pixelsPtr->Color(0, 255, 0);     // Green
            // palette[6] = pixelsPtr->Color(0, 0, 255);     // Blue
            // palette[7] = pixelsPtr->Color(255, 255, 0);   // Yellow
            paletteInitialized = true;
        }
        
        static uint8_t progress = 0;
        static int rotationOffset = 0;
        static bool useFirstPattern = true;
        
        // Switch patterns every few seconds
        if ((millis() / 5000) % 2 == 0) {  // Switch every 5 seconds
            useFirstPattern = !useFirstPattern;
        }
        
        // Get the interpolated color between two palette colours
        int currentIndex = rotationOffset;
        int nextIndex;
        if (useFirstPattern) {
            nextIndex = (rotationOffset + 1) % 4;  // First pattern uses indices 0-3
            currentIndex = currentIndex % 4;
        } else {
            nextIndex = 4 + ((rotationOffset + 1) % 4);  // Second pattern uses indices 4-7
            currentIndex = 4 + (currentIndex % 4);
        }
        
        auto interp = [&](uint32_t c1, uint32_t c2, uint8_t p) -> uint32_t {
            auto getR = [](uint32_t col) -> uint8_t { return (col >> 16) & 0xFF; };
            auto getG = [](uint32_t col) -> uint8_t { return (col >> 8) & 0xFF; };
            auto getB = [](uint32_t col) -> uint8_t { return col & 0xFF; };
            uint8_t r = getR(c1) + ((getR(c2) - getR(c1)) * p) / 255;
            uint8_t g = getG(c1) + ((getG(c2) - getG(c1)) * p) / 255;
            uint8_t b = getB(c1) + ((getB(c2) - getB(c1)) * p) / 255;
            return pixelsPtr->Color(r, g, b);
        };

        // Get the interpolated color between two palette colours
        uint32_t interpColor = interp(palette[currentIndex], palette[nextIndex], progress);
        
        // --- Modified Code: Apply sinusoidal brightness modulation using a brightness range 20 to DEFAULT_BRIGHTNESS ---
        float phase = (millis() % HOMING_BRIGHTNESS_PERIOD) / (float)HOMING_BRIGHTNESS_PERIOD;  // 2-second period
        float brightnessFactor = (sin(phase * 2 * PI - PI/2) + 1) / 2; // Range: 0 to 1
        uint8_t effectiveBrightness = HOMING_MIN_BRIGHTNESS + (uint8_t)((DEFAULT_BRIGHTNESS - HOMING_MIN_BRIGHTNESS) * brightnessFactor);
        auto getR = [](uint32_t col) -> uint8_t { return (col >> 16) & 0xFF; };
        auto getG = [](uint32_t col) -> uint8_t { return (col >> 8) & 0xFF; };
        auto getB = [](uint32_t col) -> uint8_t { return col & 0xFF; };
        uint8_t baseR = getR(interpColor);
        uint8_t baseG = getG(interpColor);
        uint8_t baseB = getB(interpColor);
        uint8_t r = (uint8_t)((baseR * effectiveBrightness) / 255);
        uint8_t g = (uint8_t)((baseG * effectiveBrightness) / 255);
        uint8_t b = (uint8_t)((baseB * effectiveBrightness) / 255);
        uint32_t modulatedColor = pixelsPtr->Color(r, g, b);
        // --- End Modified Code ---
        
        // Apply the same interpolated color to all LEDs
        for (int i = 0; i < NUM_PIXELS; i++) {
            pixelsPtr->setPixelColor(i, modulatedColor);
        }
        
        progress = min((uint16_t)255, (uint16_t)(progress + HOMING_TRANSITION_STEP));  // Fixed constant name
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
        // --- Modified Code: Run state now uses on/off timing ---
        uint16_t cycle = ON_TIME + OFF_TIME;
        uint16_t t = millis() % cycle;
        uint32_t color = (t < ON_TIME) ? COLOR_ORANGE : COLOR_OFF;
        pixelsPtr->setPixelColor(LED_PLAYPAUSE, color);
        // --- End Modified Code ---
    }

    void LedColors::pausedAnimation() {
        // --- Modified Code: Paused state now uses an orange breathing effect with asymmetric rise and fall ---
        uint16_t period = PAUSED_BREATHING_RISE + PAUSED_BREATHING_FALL;
        uint16_t t = millis() % period;
        uint8_t minB = PAUSED_MIN_BRIGHTNESS;  // Minimum brightness
        uint8_t maxB = DEFAULT_BRIGHTNESS;  // Maximum brightness (70)
        uint8_t brightness;
        if (t < PAUSED_BREATHING_RISE) {
            brightness = map(t, 0, PAUSED_BREATHING_RISE, minB, maxB);
        } else {
            brightness = map(t, PAUSED_BREATHING_RISE, period, maxB, minB);
        }
        // Scale the fixed COLOR_ORANGE (255,100,0) by brightness factor
        float factor = (float)brightness / (float)maxB;
        uint8_t r = (uint8_t)(255 * factor);
        uint8_t g = (uint8_t)(100 * factor);
        uint32_t modulatedColor = pixelsPtr->Color(r, g, 0);
        pixelsPtr->setPixelColor(LED_PLAYPAUSE, modulatedColor);
        // --- End Modified Code ---
    }

    void LedColors::idleAnimation() {
        if (millis() - lastAnimationUpdate > FADE_INTERVAL_MS) {
            lastAnimationUpdate = millis();
            fadeValue -= fadeStep;  // Fade towards off
            if (fadeValue <= 0) {
                fadeValue = 0;
            }
            uint32_t color = pixelsPtr->Color(
                map(fadeValue, 0, 255, 0, 0),      // Red component
                map(fadeValue, 0, 255, 0, 0),    // Green component
                0                                   // Blue component
            );
            pixelsPtr->setPixelColor(LED_PLAYPAUSE, color);
        }
    }

    // New method: machine initialization animation.
    void LedColors::machineInitAnimation() {
        static unsigned long initStart = millis();
        unsigned long elapsed = millis() - initStart;
        if (elapsed < 500) { // Reduced from 1000
            // First phase: wavy multicolor animation using hues except red and green.
            homingAnimation();
        } else if (elapsed < 1500) { // Reduced from 3000
            // Transition to ready state: smooth shift to green then three blinks.
            transitionToReadyAnimation();
        } else {
            inHomingMode = false;
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

    void LedColors::transitionStateColor(uint32_t fromColor, uint32_t toColor, uint16_t duration) {
        // New static variables to track the ongoing transition
        static uint32_t transitionStart = 0;
        static uint32_t lastFrom = 0;
        static uint32_t lastTo = 0;

        // If a new transition is detected, (fromColor or toColor changed), restart the timer.
        if (fromColor != lastFrom || toColor != lastTo) {
            transitionStart = millis();
            lastFrom = fromColor;
            lastTo = toColor;
        }

        uint32_t elapsed = millis() - transitionStart;
        if (elapsed >= duration) {
            // Transition complete: show target color.
            for (int i = 0; i < NUM_PIXELS; i++) {
                pixelsPtr->setPixelColor(i, toColor);
            }
            pixelsPtr->show();
            return;
        }

        // Compute interpolation progress [0, 255]
        uint8_t progress = (elapsed * 255UL) / duration;

        auto getR = [](uint32_t col) -> uint8_t { return (col >> 16) & 0xFF; };
        auto getG = [](uint32_t col) -> uint8_t { return (col >> 8) & 0xFF; };  // Fixed uint88 to uint8_t
        auto getB = [](uint32_t col) -> uint8_t { return col & 0xFF; };

        uint8_t r = getR(fromColor) + ((getR(toColor) - getR(fromColor)) * progress) / 255;
        uint8_t g = getG(fromColor) + ((getG(toColor) - getG(fromColor)) * progress) / 255;
        uint8_t b = getB(fromColor) + ((getB(toColor) - getB(fromColor)) * progress) / 255;
        uint32_t interpColor = pixelsPtr->Color(r, g, b);

        for (int i = 0; i < NUM_PIXELS; i++) {
            pixelsPtr->setPixelColor(i, interpColor);
        }
        pixelsPtr->show();
    }
}
