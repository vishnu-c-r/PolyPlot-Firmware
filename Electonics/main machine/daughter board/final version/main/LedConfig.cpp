#include "LedConfig.hpp"

namespace LEDControl {
    uint32_t LedColors::COLOR_IDLE = 0;
    uint32_t LedColors::COLOR_PRESSED = 0;
    uint32_t LedColors::COLOR_OFF = 0;
    uint32_t LedColors::COLOR_RUNNING = 0;
    uint32_t LedColors::COLOR_PAUSED = 0;
    uint32_t LedColors::COLOR_COMPLETE = 0;
    uint32_t LedColors::COLOR_ERROR = 0;
    uint32_t LedColors::COLOR_JOG = 0;


    Adafruit_NeoPixel* LedColors::pixelsPtr = nullptr;

    void LedColors::init(Adafruit_NeoPixel& pixels) {
        pixelsPtr = &pixels;  // Store the pointer
        
        // Initialize with brighter colors
        COLOR_IDLE = pixels.Color(0, 128, 128);    // Bright cyan
        COLOR_PRESSED = pixels.Color(255, 255, 255);  // Full white
        COLOR_OFF = pixels.Color(0, 0, 0);          // Off
        
        COLOR_RUNNING = pixels.Color(128, 0, 128);   // Bright purple
        COLOR_PAUSED = pixels.Color(255, 100, 0);    // Bright orange
        COLOR_COMPLETE = pixels.Color(0, 255, 0);    // Full green
        COLOR_ERROR = pixels.Color(255, 0, 0);       // Full red
        COLOR_JOG = pixels.Color(0, 0, 255);        // Full blue
        
        // Test initialization
        pixels.clear();
        pixels.show();
    }
}
