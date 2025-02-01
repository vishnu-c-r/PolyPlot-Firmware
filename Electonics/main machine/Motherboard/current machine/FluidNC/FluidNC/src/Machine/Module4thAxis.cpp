// Copyright (c) 2024 - Your Name
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Module4thAxis.h"
#include "../Logging.h"  // For logging, if required
#include "Arduino.h"

namespace Machine {

Module4thAxis::Module4thAxis(Uart* uart) : _moduleType(ModuleType::UNKNOWN), _uart(uart) {}

void Module4thAxis::init() {
    if (_uart) {
        
        detectModuleType();   // Detect the module type
    }
}

void Module4thAxis::detectModuleType() {
    if (!_uart) return;

    // Send a ping to the module
    _uart->write("PING\n");

    // Wait for response
    std::string response = "";
    long timeout = millis() + 2000;
    while (millis() < timeout) {
        if (_uart->available()) {
            response += static_cast<char>(_uart->read());
        }
        if (response.back() == '\n') {
            break;
        }
    }

    // Identify the module type based on the response
    if (response.find("PEN_MODULE") != std::string::npos) {
        _moduleType = ModuleType::PEN_MODULE;
    } else if (response.find("KNIFE_MODULE") != std::string::npos) {
        _moduleType = ModuleType::KNIFE_MODULE;
    } else if (response.find("CREASE_MODULE") != std::string::npos) {
        _moduleType = ModuleType::CREASE_MODULE;
    } else {
        _moduleType = ModuleType::UNKNOWN;
    }
}

Module4thAxis::ModuleType Module4thAxis::getModuleType() const {
    return _moduleType;
}

std::string Module4thAxis::getModuleTypeString() const {
    switch (_moduleType) {
        case ModuleType::PEN_MODULE: return "Pen Module";
        case ModuleType::KNIFE_MODULE: return "Tangential Knife Module";
        case ModuleType::CREASE_MODULE: return "Creasing Wheel Module";
        default: return "Unknown Module";
    }
}

}  // namespace Machine
