#include "LaserPointer.h"
#include "Machine/MachineConfig.h"
#include "Platform.h"

LaserPointer* LaserPointer::instance = nullptr;

LaserPointer::LaserPointer() : _pin(-1), _active_low(false), _x_offset(0), _y_offset(0), _enabled(false), _pwm_channel(0) {
    instance = this;
}

void LaserPointer::init() {
    if (_pin != -1) {
        pinMode(_pin, OUTPUT);  // Use standard Arduino pinMode
        digitalWrite(_pin, _active_low); // Set initial state
    }
}

void LaserPointer::setState(bool on) {
    if (_pin != -1) {
        _enabled = on;
        digitalWrite(_pin, on != _active_low);  // Use standard Arduino digitalWrite
    }
}

void LaserPointer::group(Configuration::HandlerBase& handler) {
    handler.item("pin", _pin);
    handler.item("active_low", _active_low);
    handler.item("x_offset", _x_offset);
    handler.item("y_offset", _y_offset);
}

void LaserPointer::afterParse() {
    if (_pin == -1) {
        log_info("Laser pointer not configured");
    }
}