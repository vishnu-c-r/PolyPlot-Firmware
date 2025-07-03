// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

// Forward declare Kinematics namespace and class
namespace Kinematics {
    class Kinematics;
}

// Move includes after forward declarations
#include "../Assert.h"
#include "../Configuration/GenericFactory.h"
#include "../Configuration/HandlerBase.h"
#include "../Configuration/Configurable.h"
#include "../CoolantControl.h"
#include "../Kinematics/Kinematics.h"
#include "../Control.h"
#include "../Probe.h"
#include "src/Parking.h"
#include "../SDCard.h"
#include "../Stepping.h"
#include "../Stepper.h"
#include "../Config.h"
#include "../OLED.h"
#include "../Status_outputs.h"
#include "../UartChannel.h"
#include "Axes.h"
#include "SPIBus.h"
#include "I2CBus.h"
#include "I2SOBus.h"
#include "UserOutputs.h"
#include "Macros.h"

#include <string_view>

namespace Machine {
    using ::Kinematics::Kinematics;

    class Start : public Configuration::Configurable {
    public:
        bool _mustHome          = true;
        bool _deactivateParking = false;

        // At power-up or a reset, the limit switches will be checked
        // to ensure they are not already active. If so, and hard
        // limits are enabled, Alarm state will be entered instead of
        // Idle and the user will be told to check the limits.
        bool _checkLimits = true;

    public:
        Start() {}

        void group(Configuration::HandlerBase& handler) {
            handler.item("must_home", _mustHome);
            handler.item("deactivate_parking", _deactivateParking);
            handler.item("check_limits", _checkLimits);
        }

        ~Start() = default;
    };

    class WorkArea : public Configuration::Configurable {
    public:
        float _minX                    = -1000.0f;  // Default to very large area if not configured
        float _minY                    = -1000.0f;
        float _maxX                    = 1000.0f;
        float _maxY                    = 1000.0f;
        float _originX                 = 0.0f;  // The X coordinate to move to after homing
        float _originY                 = 0.0f;  // The Y coordinate to move to after homing
        bool  _enabled                 = true;  // Add enable/disable flag
        bool  _moveToOriginAfterHoming = true;  // Whether to move to origin after homing

    public:
        WorkArea() {}

        void group(Configuration::HandlerBase& handler) {
            handler.item("min_x", _minX);
            handler.item("min_y", _minY);
            handler.item("max_x", _maxX);
            handler.item("max_y", _maxY);
            handler.item("origin_x", _originX);
            handler.item("origin_y", _originY);
            handler.item("enabled", _enabled);
            handler.item("move_to_origin", _moveToOriginAfterHoming);
        }

        ~WorkArea() = default;
    };

    class MachineConfig : public Configuration::Configurable {
    public:
        MachineConfig() = default;

        Axes*                     _axes           = nullptr;
        ::Kinematics::Kinematics* _kinematics     = nullptr;
        SPIBus*                   _spi            = nullptr;
        I2CBus*                   _i2c[MAX_N_I2C] = { nullptr };
        I2SOBus*                  _i2so           = nullptr;
        Stepping*                 _stepping       = nullptr;
        CoolantControl*           _coolant        = nullptr;
        Probe*                    _probe          = nullptr;
        Control*                  _control        = nullptr;
        UserOutputs*              _userOutputs    = nullptr;
        SDCard*                   _sdCard         = nullptr;
        Macros*                   _macros         = nullptr;
        Start*                    _start          = nullptr;
        WorkArea*                 _workArea       = nullptr;
        Parking*                  _parking        = nullptr;
        OLED*                     _oled           = nullptr;
        Status_Outputs*           _stat_out       = nullptr;

        UartChannel* _uart_channels[MAX_N_UARTS] = { nullptr };
        Uart*        _uarts[MAX_N_UARTS]         = { nullptr };

        float _arcTolerance      = 0.002f;
        float _junctionDeviation = 0.01f;
        bool  _verboseErrors     = true;
        bool  _reportInches      = false;

        size_t _planner_blocks = 16;

        float _laserOffsetX = 0.0f;
        float _laserOffsetY = 0.0f;

        // Add a getter method for the module type
        // String getModuleType() const { return _module_type; }

        // Enables a special set of M-code commands that enables and disables the parking motion.
        // These are controlled by `M56`, `M56 P1`, or `M56 Px` to enable and `M56 P0` to disable.
        // The command is modal and will be set after a planner sync. Since it is GCode, it is
        // executed in sync with GCode commands. It is not a real-time command.
        bool _enableParkingOverrideControl = false;

        // Tracks and reports gcode line numbers. Disabled by default.
        bool _useLineNumbers = false;

        std::string _board = "None";
        std::string _name  = "None";
        std::string _meta  = "";
#if 1
        static MachineConfig*& instance() {
            static MachineConfig* instance = nullptr;
            return instance;
        }
#endif

        void afterParse() override;
        void group(Configuration::HandlerBase& handler) override;
        // void setModuleType(const String& moduleType) { _module_type = moduleType; }

        static void load();
        static void load_file(std::string_view file);
        static void load_yaml(std::string_view yaml_string);

        float getLaserOffsetX() const { return _laserOffsetX; }
        float getLaserOffsetY() const { return _laserOffsetY; }
        void  setLaserOffset(float x, float y) {
            _laserOffsetX = x;
            _laserOffsetY = y;
        }

        // Work area getter methods
        bool  useWorkAreaLimits() const { return _workArea != nullptr && _workArea->_enabled; }
        float getWorkAreaMinX() const { return _workArea ? _workArea->_minX : -1000.0f; }
        float getWorkAreaMinY() const { return _workArea ? _workArea->_minY : -1000.0f; }
        float getWorkAreaMaxX() const { return _workArea ? _workArea->_maxX : 1000.0f; }
        float getWorkAreaMaxY() const { return _workArea ? _workArea->_maxY : 1000.0f; }

        // Work area control methods
        void enableWorkArea() {
            if (_workArea)
                _workArea->_enabled = true;
        }
        void disableWorkArea() {
            if (_workArea)
                _workArea->_enabled = false;
        }

        ~MachineConfig();
    };
}

extern Machine::MachineConfig* config;

template <typename T>
void copyAxes(T* dest, T* src) {
    auto n_axis = config->_axes->_numberAxis;
    for (size_t axis = 0; axis < n_axis; axis++) {
        dest[axis] = src[axis];
    }
}
