#include "LaserPointer.h"
#include "Machine/MachineConfig.h"
#include "Platform.h"

LaserPointer* LaserPointer::instance = nullptr;

LaserPointer::LaserPointer() : _xOffset(0), _yOffset(0), _enabled(false) {
    instance = this;
}

void LaserPointer::init() {
    if (_laserPin.defined()) {
        _laserPin.setAttr(Pin::Attr::Output);
        _laserPin.write(false);
    }
}

void LaserPointer::setState(bool on) {
    if (_laserPin.defined()) {
        _enabled = on;
        _laserPin.write(on);
    }
}

void LaserPointer::group(Configuration::HandlerBase& handler) {
    handler.item("laserPointer_pin", _laserPin);
    handler.item("x_offset", _xOffset, -1000, 1000);  // Range: -1000mm to +1000mm
    handler.item("y_offset", _yOffset, -1000, 1000);  // Range: -1000mm to +1000mm
}

void LaserPointer::afterParse() {
    if (!_laserPin.defined()) {
        log_info("Laser pointer not configured");
        return;
    }
    log_info("Laser pointer configured on pin " << _laserPin.name() << " offset X:" << _xOffset << " Y:" << _yOffset);
}