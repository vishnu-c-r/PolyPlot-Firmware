// Copyright (c) 2024 - Your Name
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Uart.h"  // Include the UART class for communication
#include <string>

namespace Machine {

class Module4thAxis {
public:
    enum class ModuleType {
        UNKNOWN,
        PEN_MODULE,
        KNIFE_MODULE,
        CREASE_MODULE
    };

private:
    ModuleType _moduleType;
    Uart* _uart;

public:
    Module4thAxis(Uart* uart);

    void init();
    void detectModuleType();
    ModuleType getModuleType() const;
    std::string getModuleTypeString() const;
};

}  // namespace Machine
