#pragma once

#include "Configuration/Configurable.h"
#include "Pin.h"  // Add this include for Pin definitions
#include <cstdint>

class LaserPointer : public Configuration::Configurable {
private:
    int      _pin;
    bool     _active_low;
    float    _x_offset;  // Offset between laser and pen in X axis
    float    _y_offset;  // Offset between laser and pen in Y axis
    bool     _enabled;
    uint32_t _pwm_channel;

public:
    LaserPointer();
    
    void init();
    void setState(bool on);
    bool getState() const { return _enabled; }
    
    float getXOffset() const { return _x_offset; }
    float getYOffset() const { return _y_offset; }
    
    bool isAvailable() const { return _pin != -1; }  // Add this method
    
    // Configuration handlers
    void group(Configuration::HandlerBase& handler) override;
    void afterParse() override;
    
    // Static instance
    static LaserPointer* instance;
};