#include "ToolConfig.h"
#include "../FileStream.h"
#include "../GCode.h"  // For MAX_PENS
#include "JSONEncoder.h"

namespace WebUI {

    bool ToolConfig::loadConfig() {
        try {
            FileStream file(configPath, "r");
            if (!file) {
                log_error("Failed to open tool config file");
                return false;
            }

            std::string jsonStr;
            char        buf[256];
            size_t      len;
            while ((len = file.read(buf, sizeof(buf))) > 0) {
                jsonStr.append(buf, len);
            }

            if (jsonStr.empty()) {
                log_error("Empty tool config file");
                return false;
            }

            return fromJSON(jsonStr);

        } catch (const Error& err) {
            log_error("Error loading tool config: " << (int)err);
            return false;
        }
    }

    bool ToolConfig::saveConfig() {
        try {
            {  // Scope for automatic file closure
                FileStream  file(configPath, "w");
                std::string jsonStr = toJSON();
                file.write(reinterpret_cast<const uint8_t*>(jsonStr.c_str()), jsonStr.length());
            }  // File closed automatically here
            return true;
        } catch (const Error err) {
            log_error("Failed to save tool config");
            return false;
        }
    }

    bool ToolConfig::addTool(const Tool& tool) {
        if (getTool(tool.number)) {
            return false;
        }
        tools.push_back(tool);
        return true;
    }

    bool ToolConfig::updateTool(const Tool& tool) {
        for (auto& existingTool : tools) {
            if (existingTool.number == tool.number) {
                existingTool = tool;
                return true;
            }
        }
        return false;
    }

    bool ToolConfig::deleteTool(int number) {
        for (auto it = tools.begin(); it != tools.end(); ++it) {
            if (it->number == number) {
                tools.erase(it);
                return true;
            }
        }
        return false;
    }

    Tool* ToolConfig::getTool(int number) {
        for (auto& tool : tools) {
            if (tool.number == number) {
                return &tool;
            }
        }
        return nullptr;
    }

    std::string ToolConfig::toJSON() {
        std::string output;
        JSONencoder j(&output);  // Add WebUI namespace

        j.begin();
        j.begin_array("tools");

        for (const auto& tool : tools) {
            j.begin_object();
            j.member("number", std::to_string(tool.number).c_str());
            j.member("x", std::to_string(tool.x).c_str());
            j.member("y", std::to_string(tool.y).c_str());
            j.member("z", std::to_string(tool.z).c_str());
            j.member("occupied", tool.occupied ? "true" : "false");
            j.end_object();
        }

        j.end_array();
        j.end();
        return output;
    }

    bool ToolConfig::fromJSON(const std::string& jsonStr) {
        tools.clear();

        size_t arrayStart = jsonStr.find("[");
        if (arrayStart == std::string::npos) {
            log_error("No array start found in JSON");
            return false;
        }

        size_t pos = arrayStart;
        while ((pos = jsonStr.find("{", pos)) != std::string::npos) {
            size_t end = jsonStr.find("}", pos);
            if (end == std::string::npos)
                break;

            std::string obj = jsonStr.substr(pos, end - pos + 1);
            // log_debug("Parsing tool object: " << obj);

            Tool tool;
            bool validTool = true;

            // Parse tool number
            size_t numPos = obj.find("\"number\":");
            if (numPos != std::string::npos) {
                std::string numStr = obj.substr(numPos + 9);
                // Remove quotes if present
                if (numStr[0] == '"') {
                    numStr = numStr.substr(1, numStr.find('"', 1) - 1);
                }
                tool.number = atoi(numStr.c_str());
                // log_debug("Parsed tool number: " << tool.number);
            } else {
                validTool = false;
            }

            // Parse X coordinate
            size_t xPos = obj.find("\"x\":");
            if (xPos != std::string::npos) {
                std::string xStr = obj.substr(xPos + 4);
                if (xStr[0] == '"') {
                    xStr = xStr.substr(1, xStr.find('"', 1) - 1);
                }
                tool.x = atof(xStr.c_str());
                // log_debug("Parsed X: " << tool.x);
            } else {
                validTool = false;
            }

            // Parse Y coordinate
            size_t yPos = obj.find("\"y\":");
            if (yPos != std::string::npos) {
                std::string yStr = obj.substr(yPos + 4);
                if (yStr[0] == '"') {
                    yStr = yStr.substr(1, yStr.find('"', 1) - 1);
                }
                tool.y = atof(yStr.c_str());
                // log_debug("Parsed Y: " << tool.y);
            } else {
                validTool = false;
            }

            // Parse Z coordinate with more detailed logging
            size_t zPos = obj.find("\"z\":");
            if (zPos != std::string::npos) {
                std::string zStr = obj.substr(zPos + 4);
                // log_debug("Raw Z string: " << zStr);
                if (zStr[0] == '"') {
                    zStr = zStr.substr(1, zStr.find('"', 1) - 1);
                    // log_debug("Unquoted Z string: " << zStr);
                }
                tool.z = atof(zStr.c_str());
                if (tool.z == 0.0f && zStr != "0" && zStr != "0.0") {
                    log_error("Z value parsing failed for input: " << zStr);
                    validTool = false;
                } else {
                    // log_debug("Successfully parsed Z value: " << tool.z);
                }
            } else {
                log_error("No Z coordinate found in tool data");
                validTool = false;
            }

            // Parse occupied status
            size_t occupiedPos = obj.find("\"occupied\":");
            if (occupiedPos != std::string::npos) {
                std::string occupiedStr = obj.substr(occupiedPos + 10);
                if (occupiedStr[0] == '"') {
                    occupiedStr = occupiedStr.substr(1, occupiedStr.find('"', 1) - 1);
                }
                tool.occupied = (occupiedStr == "true");
                // log_debug("Parsed occupied: " << tool.occupied);
            }

            if (validTool) {
                tools.push_back(tool);
                // log_debug("Added tool " << tool.number);
            } else {
                log_error("Invalid tool data in JSON object");
            }

            pos = end + 1;
        }

        // // log_debug("Loaded " << tools.size() << " tools from config");
        return !tools.empty();
    }

    // Functions to replace Pen namespace functionality
    bool ToolConfig::getToolPosition(int toolNumber, float* position) {
        Tool* tool = getTool(toolNumber);
        if (!tool) {
            log_error("Tool " << toolNumber << " not found in config");
            return false;
        }
        position[0] = tool->x;
        position[1] = tool->y;
        position[2] = tool->z;
        // log_debug("Tool " << toolNumber << " position: x=" << tool->x << " y=" << tool->y << " z=" << tool->z);
        return true;
    }

    bool ToolConfig::isToolOccupied(int toolNumber) {
        Tool* tool = getTool(toolNumber);
        return tool ? tool->occupied : false;
    }

    void ToolConfig::setToolOccupied(int toolNumber, bool state) {
        Tool* tool = getTool(toolNumber);
        if (tool) {
            tool->occupied = state;
            saveConfig();
        }
    }

    bool ToolConfig::saveCurrentState(int currentPen) {
        try {
            {  // Scope for automatic file closure
                FileStream  file(stateFile, "w");
                std::string output;
                JSONencoder j(&output);  // Fix: Pass string pointer to constructor
                j.begin();
                j.member("currentPen", std::to_string(currentPen).c_str());
                j.member("timestamp", std::to_string(esp_timer_get_time()).c_str());
                j.end();
                file.write(reinterpret_cast<const uint8_t*>(output.c_str()), output.length());  // Fix: Use output string
            }  // File closed automatically here
            return true;
        } catch (const Error err) {
            log_error("Failed to save pen state");
            return false;
        }
    }

    int ToolConfig::getLastKnownState() {
        try {
            {  // Scope for automatic file closure
                FileStream  file(stateFile, "r");
                std::string jsonStr;
                char        buf[256];
                size_t      len;
                while ((len = file.read(buf, sizeof(buf))) > 0) {
                    jsonStr.append(buf, len);
                }
                // File closed automatically here

                size_t penPos = jsonStr.find("\"currentPen\":");
                if (penPos != std::string::npos) {
                    return atoi(jsonStr.substr(penPos + 12).c_str());
                }
            }
        } catch (const Error err) { log_info("No saved pen state found"); }
        return 0;  // Default to no pen loaded
    }

    bool ToolConfig::checkCollisionRisk(int fromPen, int toPen) {
        if (fromPen == toPen)
            return false;

        auto fromPos = getTool(fromPen);
        auto toPos   = getTool(toPen);
        if (!fromPos || !toPos)
            return false;

        // Check if movement path crosses other pen positions
        // Based on your JSON, pens are arranged vertically along X=-496.0
        // with Y positions at intervals of about 41.2mm
        for (const auto& pos : tools) {
            if (pos.number == fromPen || pos.number == toPen)
                continue;

            // Check if path intersects with another pen position
            float minY = std::min(fromPos->y, toPos->y);
            float maxY = std::max(fromPos->y, toPos->y);

            if (pos.y > minY && pos.y < maxY) {
                log_error("Collision risk detected with pen " << pos.number);
                return true;
            }
        }
        return false;
    }

    bool ToolConfig::validatePosition(const Tool& pos) {
        // Check for valid ranges based on your toolconfig.json values
        if (pos.x < -500 || pos.x > 0 ||  // X range
            pos.y < -300 || pos.y > 0 ||  // Y range based on your values (-33.5 to -239.5)
            pos.z < -20 || pos.z > 0) {   // Z range based on your -20.0 value
            log_error("Position out of valid range");
            return false;
        }

        // Check tool number range (1-6 based on your JSON)
        if (pos.number < 1 || pos.number > 6) {
            log_error("Invalid tool number");
            return false;
        }

        return true;
    }

    bool ToolConfig::parseJsonNumber(const std::string& json, const char* key, int& value) {
        std::string keyStr = "\"" + std::string(key) + "\":";
        size_t      pos    = json.find(keyStr);
        if (pos != std::string::npos) {
            pos += keyStr.length();
            // Skip whitespace
            while (pos < json.length() && std::isspace(json[pos]))
                pos++;

            // Handle both quoted and unquoted numbers
            if (pos < json.length()) {
                if (json[pos] == '"') {
                    pos++;  // Skip opening quote
                    value = std::atoi(json.substr(pos).c_str());
                    return true;
                } else {
                    value = std::atoi(json.substr(pos).c_str());
                    return true;
                }
            }
        }
        return false;
    }

    bool ToolConfig::parseJsonFloat(const std::string& json, const char* key, float& value) {
        std::string keyStr = "\"" + std::string(key) + "\":";
        size_t      found  = json.find(keyStr);
        if (found != std::string::npos) {
            found += keyStr.size();
            // Skip whitespace
            while (found < json.size() && std::isspace(json[found])) {
                found++;
            }
            // Handle both quoted and unquoted numbers
            if (found < json.size()) {
                if (found == '"') {
                    found++;  // Skip opening quote
                    value = std::strtof(json.substr(found).c_str(), nullptr);
                    return true;
                } else {
                    value = std::strtof(json.substr(found).c_str(), nullptr);
                    return true;
                }
            }
        }
        return false;
    }

    ToolStatus ToolConfig::getStatus() {
        ToolStatus status;

        // Get current pen from state file
        status.currentPen = getLastKnownState();
        status.totalPens  = MAX_TOOLS;

        // Build pen status array
        status.penStatus.resize(MAX_TOOLS);
        for (int i = 0; i < MAX_TOOLS; i++) {
            status.penStatus[i] = isToolOccupied(i + 1);
        }

        // TODO: Connect to motion control system
        // status.inMotion = Machine::MotionControl::isMoving();
        status.inMotion = false;  // Temporary until motion control integration

        // TODO: Implement error tracking
        status.error     = false;
        status.lastError = "";

        // Report status through serial interface
        reportStatus();

        return status;
    }

    void ToolConfig::reportStatus() {
        // Report current tool status through serial interface
        log_info("Tool Status Report:");
        log_info("Current Pen: " << getLastKnownState());
        log_info("Total Tools: " << MAX_TOOLS);

        // Report individual tool states
        for (int i = 1; i <= MAX_TOOLS; i++) {
            auto* pos = getTool(i);
            if (pos) {
                log_info("Tool " << i << ": " << (pos->occupied ? "Occupied" : "Empty") << " at X:" << pos->x << " Y:" << pos->y
                                 << " Z:" << pos->z);
            } else {
                log_info("Tool " << i << ": Not configured");
            }
        }

        // Report any errors
        if (tools.empty()) {
            log_warn("No tool positions configured");
        }
    }

    bool ToolConfig::ensureLoaded() {
        if (tools.empty()) {  // Changed from _tools to tools
            return loadConfig();
        }
        return true;
    }
}
