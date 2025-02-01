#pragma once

#include "Configuration/Configurable.h"
#include "Pin.h"

class LaserPointer : public Configuration::Configurable {
private:
    Pin     _laserPin;
    int32_t _xOffset;  // Integer offset in mm
    int32_t _yOffset;  // Integer offset in mm
    bool    _enabled;

public:
    LaserPointer();
    
    void init();
    void setState(bool on);
    bool getState() const { return _enabled; }
    
    int32_t getXOffset() const { return _xOffset; }
    int32_t getYOffset() const { return _yOffset; }
    
    bool isAvailable() const { return _laserPin.defined(); }
    
    void group(Configuration::HandlerBase& handler) override;
    void afterParse() override;
    
    static LaserPointer* instance;
};