#include "ToolConfig.h"
#include "../FileStream.h"
#include "../WebUI/JSONEncoder.h"

namespace Machine {

    bool ToolConfig::loadConfig() {
        try {
            log_debug("Loading tool config from " << configPath);
            
            FileStream file(configPath, "r");
            std::string jsonStr;
            char buf[256];
            size_t len;
            while ((len = file.read(buf, sizeof(buf))) > 0) {
                jsonStr.append(buf, len);
            }
            
            log_debug("Read JSON string: " << jsonStr);
            
            bool result = fromJSON(jsonStr);
            if (result) {
                log_debug("Successfully loaded " << tools.size() << " tools");
                // Add debug output for each tool
                for (const auto& tool : tools) {
                    log_debug("Tool " << tool.number << ": x=" << tool.x << " y=" << tool.y << " z=" << tool.z << " occupied=" << tool.occupied);
                }
            } else {
                log_error("Failed to parse tool config JSON");
            }
            return result;
        } catch (const Error& err) {
            log_error("Error loading tool config file - does it exist at " << configPath << "?");
            tools.clear();
            return false;
        }
    }

    bool ToolConfig::saveConfig() {
        try {
            FileStream file(configPath, "w");
            std::string jsonStr = toJSON();
            file.write(reinterpret_cast<const uint8_t*>(jsonStr.c_str()), jsonStr.length());
            return true;
        } catch (const Error err) {
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
        WebUI::JSONencoder j(&output);  // Add WebUI namespace

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
            if (end == std::string::npos) break;

            std::string obj = jsonStr.substr(pos, end - pos + 1);
            log_debug("Parsing tool object: " << obj);
            
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
                log_debug("Parsed tool number: " << tool.number);
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
                log_debug("Parsed X: " << tool.x);
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
                log_debug("Parsed Y: " << tool.y);
            } else {
                validTool = false;
            }

            // Parse Z coordinate with more detailed logging
            size_t zPos = obj.find("\"z\":");
            if (zPos != std::string::npos) {
                std::string zStr = obj.substr(zPos + 4);
                log_debug("Raw Z string: " << zStr);
                if (zStr[0] == '"') {
                    zStr = zStr.substr(1, zStr.find('"', 1) - 1);
                    log_debug("Unquoted Z string: " << zStr);
                }
                tool.z = atof(zStr.c_str());
                if (tool.z == 0.0f && zStr != "0" && zStr != "0.0") {
                    log_error("Z value parsing failed for input: " << zStr);
                    validTool = false;
                } else {
                    log_debug("Successfully parsed Z value: " << tool.z);
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
                log_debug("Parsed occupied: " << tool.occupied);
            }

            if (validTool) {
                tools.push_back(tool);
                log_debug("Added tool " << tool.number);
            } else {
                log_error("Invalid tool data in JSON object");
            }
            
            pos = end + 1;
        }

        log_debug("Loaded " << tools.size() << " tools from config");
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
        log_debug("Tool " << toolNumber << " position: x=" << tool->x << " y=" << tool->y << " z=" << tool->z);
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
}
