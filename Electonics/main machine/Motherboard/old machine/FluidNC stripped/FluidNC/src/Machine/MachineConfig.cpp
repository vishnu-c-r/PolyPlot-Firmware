// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "MachineConfig.h"

#include "../Kinematics/Kinematics.h"

#include "../Motors/MotorDriver.h"
#include "../Motors/NullMotor.h"

#include "../UartChannel.h"

#include "../SettingsDefinitions.h"  // config_filename
#include "../FileStream.h"

#include "../Configuration/Parser.h"
#include "../Configuration/ParserHandler.h"
#include "../Configuration/Validator.h"
#include "../Configuration/AfterParse.h"
#include "../Configuration/ParseException.h"
#include "../Config.h"  // ENABLE_*


#include <cstdio>
#include <cstring>
#include <atomic>
#include <memory>

Machine::MachineConfig* config;

// TODO FIXME: Split this file up into several files, perhaps put it in some folder and namespace Machine?

namespace Machine {
    void MachineConfig::group(Configuration::HandlerBase& handler) {
        handler.item("board", _board);
        handler.item("name", _name);
        handler.item("meta", _meta);

        handler.section("stepping", _stepping);

        handler.section("uart1", _uarts[1], 1);
        handler.section("uart2", _uarts[2], 2);

        handler.section("uart_channel1", _uart_channels[1], 1);
        handler.section("uart_channel2", _uart_channels[2], 2);

        handler.section("i2so", _i2so);

        handler.section("i2c0", _i2c[0], 0);
        handler.section("i2c1", _i2c[1], 1);

        handler.section("spi", _spi);
        handler.section("sdcard", _sdCard);

        handler.section("kinematics", _kinematics);
        handler.section("axes", _axes);

        handler.section("control", _control);
        handler.section("coolant", _coolant);
        handler.section("probe", _probe);
        handler.section("macros", _macros);
        handler.section("start", _start);
        handler.section("parking", _parking);

        handler.section("user_outputs", _userOutputs);

        handler.section("oled", _oled);
        handler.section("status_outputs", _stat_out);

        // TODO: Consider putting these under a gcode: hierarchy level? Or motion control?
        handler.item("arc_tolerance_mm", _arcTolerance, 0.001, 1.0);
        handler.item("junction_deviation_mm", _junctionDeviation, 0.01, 1.0);
        handler.item("verbose_errors", _verboseErrors);
        handler.item("report_inches", _reportInches);
        handler.item("enable_parking_override_control", _enableParkingOverrideControl);
        handler.item("use_line_numbers", _useLineNumbers);
        handler.item("planner_blocks", _planner_blocks, 10, 120);
    }

    void MachineConfig::afterParse() {
        if (_axes == nullptr) {
            log_info("Axes: using defaults");
            _axes = new Axes();
        }

        if (_coolant == nullptr) {
            _coolant = new CoolantControl();
        }

        if (_kinematics == nullptr) {
            _kinematics = new Kinematics();
        }

        if (_probe == nullptr) {
            _probe = new Probe();
        }

        if (_userOutputs == nullptr) {
            _userOutputs = new UserOutputs();
        }

        if (_sdCard == nullptr) {
            _sdCard = new SDCard();
        }

        if (_spi == nullptr) {
            _spi = new SPIBus();
        }

        if (_stepping == nullptr) {
            _stepping = new Stepping();
        }

        // We do not auto-create an I2SO bus config node
        // Only if an i2so section is present will config->_i2so be non-null

        if (_control == nullptr) {
            _control = new Control();
        }

        if (_start == nullptr) {
            _start = new Start();
        }

        if (_parking == nullptr) {
            _parking = new Parking();
        }

        if (_macros == nullptr) {
            _macros = new Macros();
        }

        // detectModuleType();
    }

    const char defaultConfig[] = "name: Default (Test Drive)\nboard: None\n";

    void MachineConfig::load() {
        // If the system crashes we skip the config file and use the default
        // builtin config.  This helps prevent reset loops on bad config files.
        esp_reset_reason_t reason = esp_reset_reason();
        if (reason == ESP_RST_PANIC) {
            log_error("Skipping configuration file due to panic");
            log_info("Using default configuration");
            load_yaml(defaultConfig);
            set_state(State::ConfigAlarm);
        } else {
            load_file(config_filename->get());
        }
    }

    void MachineConfig::load_file(const std::string_view filename) {
        try {
            FileStream file(std::string { filename }, "r", "");

            auto filesize = file.size();
            if (filesize <= 0) {
                log_config_error("Configuration file:" << filename << " is empty");
                return;
            }

            auto buffer      = std::make_unique<char[]>(filesize + 1);
            buffer[filesize] = '\0';
            auto actual      = file.read(buffer.get(), filesize);
            if (actual != filesize) {
                log_config_error("Configuration file:" << filename << " read error");
                return;
            }
            log_info("Configuration file:" << filename);
            load_yaml(std::string_view { buffer.get(), filesize });
        } catch (...) {
            log_config_error("Cannot open configuration file:" << filename);
            log_info("Using default configuration");
            load_yaml(defaultConfig);
            set_state(State::ConfigAlarm);
        }
    }

    void MachineConfig::load_yaml(std::string_view input) {
        bool successful = false;
        try {
            Configuration::Parser        parser(input);
            Configuration::ParserHandler handler(parser);

            // instance() is by reference, so we can just get rid of an old instance and
            // create a new one here:
            {
                auto& machineConfig = instance();
                if (machineConfig != nullptr) {
                    delete machineConfig;
                }
                machineConfig = new MachineConfig();
            }
            config = instance();

            handler.enterSection("machine", config);

            log_debug("Running after-parse tasks");

            try {
                Configuration::AfterParse afterParse;
                config->afterParse();
                config->group(afterParse);
            } catch (std::exception& ex) { log_error("Validation error: " << ex.what()); }

            log_debug("Checking configuration");

            try {
                Configuration::Validator validator;
                config->validate();
                config->group(validator);
            } catch (std::exception& ex) { log_config_error("Validation error: " << ex.what()); }

            // log_info("Heap size after configuation load is " << uint32_t(xPortGetFreeHeapSize()));

        } catch (const Configuration::ParseException& ex) {
            log_config_error("Configuration parse error on line " << ex.LineNumber() << ": " << ex.What());
        } catch (const AssertionFailed& ex) {
            // Get rid of buffer and return
            log_config_error("Configuration loading failed: " << ex.what());
        } catch (std::exception& ex) {
            // Log exception:
            log_config_error("Configuration validation error: " << ex.what());
        } catch (...) {
            // Get rid of buffer and return
            log_config_error("Unknown error while processing config file");
        }

        std::atomic_thread_fence(std::memory_order::memory_order_seq_cst);
    }

    // void MachineConfig::detectModuleType() {
    //     // Use UART or I2C to communicate with the 4th axis module and determine its type
    //     // For example, using UART:
    //     Uart* uart = _uarts[2];  // Assuming UART2 is used for 4th axis communication
    //     if (uart) {
    //         uart->begin(9600);  // Set baud rate

    //         // Send a request signal to the 4th axis module
    //         uart->write("PING\n");

    //         // Wait for the module to respond
    //         String response = "";
    //         long   timeout  = millis() + 2000;  // 2-second timeout
    //         while (millis() < timeout) {
    //             if (uart->available()) {
    //                 response += (char)uart->read();
    //             }
    //             if (response.endsWith("\n")) {
    //                 break;
    //             }
    //         }

    //         // Process the response to determine the module type
    //         if (response.startsWith("PEN_MODULE")) {
    //             _module_type = "Pen Module";
    //         } else if (response.startsWith("KNIFE_MODULE")) {
    //             _module_type = "Tangential Knife Module";
    //         } else if (response.startsWith("CREASE_MODULE")) {
    //             _module_type = "Creasing Wheel Module";
    //         } else {
    //             _module_type = "Unknown Module";
    //         }

    //         log_info("Detected 4th axis module: " << _module_type);
    //     }
    // }

    MachineConfig::~MachineConfig() {
        delete _axes;
        delete _i2so;
        delete _coolant;
        delete _probe;
        delete _sdCard;
        delete _spi;
        delete _control;
        delete _macros;
    }
}
