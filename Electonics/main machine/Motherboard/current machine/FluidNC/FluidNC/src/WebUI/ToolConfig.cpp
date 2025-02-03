#include "ToolConfig.h"
#include "../FileStream.h"
#include "../GCode.h"  // For MAX_PENS

namespace WebUI {

    bool ToolConfig::loadConfig() {
        try {
            FileStream file(configPath, "r");
            std::string jsonStr;

            char buf[256];
            size_t len;
            while ((len = file.read(buf, sizeof(buf))) > 0) {
                jsonStr.append(buf, len);
            }

            std::vector<ToolPosition> backup = positions;
            bool parsed = fromJSON(jsonStr);
            if (!parsed) {
                log_error("Failed to parse JSON. Keeping old positions.");
                positions = backup;   // Restore old positions
                return false; // Avoid clearing or saving
            }

            return true;
        } catch (const Error err) {
            log_error("Failed to load tool config. Starting with empty configuration.");
            positions.clear();
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
            log_error("Failed to save tool config");
            return false;
        }
    }

    ToolPosition ToolConfig::getPosition(int toolNumber) {
        for (const auto& pos : positions) {
            if (pos.number == toolNumber) {
                return pos;
            }
        }
        // Return a safe fallback position
        return {toolNumber, 0, 0, 0, false};
    }

    bool ToolConfig::updatePosition(const ToolPosition& position) {
        for (auto& pos : positions) {
            if (pos.number == position.number) {
                pos = position;
                return true;
            }
        }
        positions.push_back(position);
        return true;
    }

    bool ToolConfig::isOccupied(int toolNumber) {
        for (const auto& pos : positions) {
            if (pos.number == toolNumber) {
                return pos.occupied;
            }
        }
        return false;
    }

    void ToolConfig::setOccupied(int toolNumber, bool state) {
        bool foundPen = false;
        for (auto& pos : positions) {
            if (pos.number == toolNumber) {
                pos.occupied = state; // Actually set the new state
                foundPen = true;
                break;
            }
        }
        // Optionally, if pen not found, store a new entry:
        if (!foundPen) {
            // Match struct {int number; float x; float y; float z; bool occupied;}
            positions.push_back({ toolNumber, 0.0f, 0.0f, 0.0f, state });
        }
        saveConfig();
    }

    std::string ToolConfig::toJSON() {
        std::string output;
        JSONencoder j(&output);

        j.begin();
        j.begin_array("tools");

        for (const auto& pos : positions) {
            j.begin_object();
            j.member("number", std::to_string(pos.number).c_str());
            j.member("x", std::to_string(pos.x).c_str());
            j.member("y", std::to_string(pos.y).c_str());
            j.member("z", std::to_string(pos.z).c_str());
            j.member("occupied", pos.occupied ? "true" : "false");
            j.end_object();
        }

        j.end_array();
        j.end();
        return output;
    }

    bool ToolConfig::fromJSON(const std::string& jsonStr) {
        // Keep all current positions in memory to merge
        std::vector<ToolPosition> mergedPositions = positions;

        // Parse new data into temp
        std::vector<ToolPosition> temp;
        
        // Find the tools array
        size_t arrayStart = jsonStr.find("\"tools\":");
        if (arrayStart == std::string::npos) {
            return false;
        }

        arrayStart = jsonStr.find("[", arrayStart);
        if (arrayStart == std::string::npos) {
            return false;
        }

        size_t pos = arrayStart;
        while ((pos = jsonStr.find("{", pos)) != std::string::npos) {
            size_t end = jsonStr.find("}", pos);
            if (end == std::string::npos) break;

            std::string obj = jsonStr.substr(pos, end - pos + 1);
            ToolPosition toolPos;

            // Parse number
            if (!parseJsonNumber(obj, "number", toolPos.number)) {
                continue;
            }

            // Parse x
            if (!parseJsonFloat(obj, "x", toolPos.x)) {
                continue;
            }

            // Parse y
            if (!parseJsonFloat(obj, "y", toolPos.y)) {
                continue;
            }

            // Parse z
            if (!parseJsonFloat(obj, "z", toolPos.z)) {
                continue;
            }

            // Parse occupied
            size_t occPos = obj.find("\"occupied\":");
            if (occPos != std::string::npos) {
                std::string occStr = obj.substr(occPos + 10, 5);
                toolPos.occupied = (occStr.find("true") != std::string::npos);
            }

            temp.push_back(toolPos);
            pos = end + 1;
        }

        // For each parsed entry, update or insert
        for (auto &toolPos : temp) {
            bool found = false;
            for (auto &existing : mergedPositions) {
                if (existing.number == toolPos.number) {
                    // Only update if parse was valid
                    existing.x = toolPos.x;
                    existing.y = toolPos.y;
                    existing.z = toolPos.z;
                    existing.occupied = toolPos.occupied;
                    found = true;
                    break;
                }
            }
            if (!found) {
                mergedPositions.push_back(toolPos);
            }
        }

        // If we successfully parsed at least one tool, replace positions
        if (!temp.empty()) {
            positions = mergedPositions;
            return true;
        }
        return false;
    }

    bool ToolConfig::saveCurrentState(int currentPen) {
        try {
            FileStream file(stateFile, "w");
            std::string output;
            JSONencoder j(&output);  // Fix: Pass string pointer to constructor
            j.begin();
            j.member("currentPen", std::to_string(currentPen).c_str());
            j.member("timestamp", std::to_string(esp_timer_get_time()).c_str());
            j.end();
            file.write(reinterpret_cast<const uint8_t*>(output.c_str()), output.length());  // Fix: Use output string
            return true;
        } catch (const Error err) {
            log_error("Failed to save pen state");
            return false;
        }
    }

    int ToolConfig::getLastKnownState() {
        try {
            FileStream file(stateFile, "r");
            std::string jsonStr;
            char buf[256];
            size_t len;
            while ((len = file.read(buf, sizeof(buf))) > 0) {
                jsonStr.append(buf, len);
            }
            
            size_t penPos = jsonStr.find("\"currentPen\":");
            if (penPos != std::string::npos) {
                return atoi(jsonStr.substr(penPos + 12).c_str());
            }
        } catch (const Error err) {
            log_info("No saved pen state found");
        }
        return 0; // Default to no pen loaded
    }

    bool ToolConfig::checkCollisionRisk(int fromPen, int toPen) {
        if (fromPen == toPen) return false;
        
        auto fromPos = getPosition(fromPen);
        auto toPos = getPosition(toPen);
        
        // Check if movement path crosses other pen positions
        for (const auto& pos : positions) {
            if (pos.number == fromPen || pos.number == toPen) continue;
            
            // Simple collision check - any pen position between source and destination
            if (pos.y > std::min(fromPos.y, toPos.y) && 
                pos.y < std::max(fromPos.y, toPos.y) &&
                std::abs(pos.x - fromPos.x) < 10.0f) { // 10mm safety margin
                log_error("Collision risk detected with pen " << pos.number);
                return true;
            }
        }
        return false;
    }

    bool ToolConfig::validatePosition(const ToolPosition& pos) {
        // Check for valid ranges based on machine limits
        if (pos.x < -500 || pos.x > 0 ||      // X range
            pos.y < -300 || pos.y > 0 ||      // Y range
            pos.z < -20 || pos.z > 0) {       // Z range
            log_error("Position out of valid range");
            return false;
        }
        return true;
    }

    bool ToolConfig::parseJsonNumber(const std::string& json, const char* key, int& value) {
        std::string keyStr = "\"" + std::string(key) + "\":";
        size_t pos = json.find(keyStr);
        if (pos != std::string::npos) {
            pos += keyStr.length();
            // Skip whitespace
            while (pos < json.length() && std::isspace(json[pos])) pos++;
            
            // Handle both quoted and unquoted numbers
            if (pos < json.length()) {
                if (json[pos] == '"') {
                    pos++; // Skip opening quote
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
        size_t found = json.find(keyStr);
        if (found != std::string::npos) {
            found += keyStr.size();
            // Skip whitespace
            while (found < json.size() && std::isspace(json[found])) {
                found++;
            }
            // Handle both quoted and unquoted numbers
            if (found < json.size()) {
                if (json[found] == '"') {
                    found++; // Skip opening quote
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
}
