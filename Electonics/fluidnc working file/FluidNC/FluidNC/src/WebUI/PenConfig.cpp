#include "PenConfig.h"
#include "../FileStream.h"
#include "JSONEncoder.h"
#include <string>

namespace WebUI {

    bool PenConfig::loadConfig() {
        try {
            FileStream  file(configPath, "r");
            std::string jsonStr;

            // Read file content
            char   buf[256];
            size_t len;
            while ((len = file.read(buf, sizeof(buf))) > 0) {
                jsonStr.append(buf, len);
            }

            return fromJSON(jsonStr);
        } catch (const Error err) {
            // If file doesn't exist, start with empty config
            pens.clear();
            return true;
        }
    }

    bool PenConfig::saveConfig() {
        try {
            FileStream  file(configPath, "w");
            std::string jsonStr = toJSON();
            // Fix: Cast to uint8_t* for write
            file.write(reinterpret_cast<const uint8_t*>(jsonStr.c_str()), jsonStr.length());
            return true;
        } catch (const Error err) { return false; }
    }

    bool PenConfig::addPen(const Pen& pen) {
        // Check if pen with same name already exists
        for (const auto& existingPen : pens) {
            if (existingPen.name == pen.name) {
                return false;
            }
        }
        pens.push_back(pen);
        return true;
    }

    bool PenConfig::updatePen(const Pen& pen) {
        for (auto& existingPen : pens) {
            if (existingPen.name == pen.name) {
                existingPen = pen;
                return true;
            }
        }
        return false;
    }

    bool PenConfig::deletePen(const std::string& name) {
        for (auto it = pens.begin(); it != pens.end(); ++it) {
            if (it->name == name) {
                pens.erase(it);
                return true;
            }
        }
        return false;
    }

    std::string PenConfig::toJSON() {
        std::string output;
        JSONencoder j(&output);

        j.begin();
        j.begin_array("pens");

        for (const auto& pen : pens) {
            j.begin_object();
            j.member("color", pen.color.c_str());
            j.member("name", pen.name.c_str());
            j.member("zValue", std::to_string(pen.zValue).c_str());
            
            // Handle penPick array
            j.begin_array("penPick");
            for (const auto& pick : pen.penPick) {
                j.string(pick.c_str());
            }
            j.end_array();

            // Handle penDrop array
            j.begin_array("penDrop");
            for (const auto& drop : pen.penDrop) {
                j.string(drop.c_str());
            }
            j.end_array();

            j.member("skipped", pen.skipped ? "true" : "false");
            j.end_object();
        }

        j.end_array();
        j.end();
        return output;
    }

    bool PenConfig::fromJSON(const std::string& jsonStr) {
        pens.clear();
        log_debug("Parsing JSON: " << jsonStr);  // Add debug logging

        // Find the pens array
        size_t arrayStart = jsonStr.find("[");
        if (arrayStart == std::string::npos) {
            log_debug("No array found in JSON");  // Add debug logging
            return false;
        }

        size_t pos = arrayStart;
        while ((pos = jsonStr.find("{", pos)) != std::string::npos) {
            size_t end = jsonStr.find("}", pos);
            if (end == std::string::npos)
                break;

            std::string obj = jsonStr.substr(pos, end - pos + 1);
            log_debug("Processing object: " << obj);  // Add debug logging

            Pen pen;
            // Parse color
            size_t colorPos = obj.find("\"color\":");
            if (colorPos != std::string::npos) {
                size_t start = obj.find("\"", colorPos + 8) + 1;
                size_t end   = obj.find("\"", start);
                pen.color    = obj.substr(start, end - start);
            }
        }

        // Try matching just the number after T
        size_t tPos = command.find('T');
        if (tPos != std::string::npos) {
            std::string penNum = command.substr(tPos + 1);
            std::string searchCmd = "M6T" + penNum;  // Reconstruct full command
            log_debug("Trying alternate format: " << searchCmd);
            
            for (const auto& cfg : configs) {
                if (strcasecmp(cfg.PenGcode.c_str(), searchCmd.c_str()) == 0) {
                    x = cfg.x;
                    y = cfg.y;
                    z = cfg.z;
                    log_debug("Found match by reconstructed command");
                    return true;
                }
            }
        }

        log_error("No configuration found for " << command);
        return false;
    }

    // Add parseFloat helper function
    bool PenConfig::parseFloat(const std::string& str, const char* key, float& value) {
        std::string keyStr = "\"" + std::string(key) + "\":";
        size_t pos = str.find(keyStr);
        if (pos != std::string::npos) {
            value = atof(str.substr(pos + keyStr.length()).c_str());
            return true;
        }
        return false;
    }

    bool PenConfig::parseStringField(const std::string& obj, const char* field, std::string& value, const std::string& defaultVal) {
        std::string fieldStr = "\"" + std::string(field) + "\":";
        size_t pos = obj.find(fieldStr);
        if (pos != std::string::npos) {
            pos += fieldStr.length();
            while (pos < obj.length() && (obj[pos] == ' ' || obj[pos] == '"')) {
                pos++;
            }
            
            // Skip array notation if present
            if (obj[pos] == '[') {
                pos++;
                // Skip any nested object structure
                while (pos < obj.length() && (obj[pos] == ' ' || obj[pos] == '{' || obj[pos] == '"')) {
                    pos++;
                }
            }
            
            size_t end = obj.find_first_of(",}]", pos);
            if (end != std::string::npos) {
                // Clean up the value by removing any extra formatting
                std::string rawValue = obj.substr(pos, end - pos);
                // Remove any trailing quotes, spaces, or object notation
                while (!rawValue.empty() && (rawValue.back() == '"' || rawValue.back() == ' ' || rawValue.back() == '}')) {
                    rawValue.pop_back();
                }
                // Remove any "0": prefix if present
                size_t colonPos = rawValue.find(":");
                if (colonPos != std::string::npos) {
                    value = rawValue.substr(colonPos + 1);
                } else {
                    value = rawValue;
                }
                // Trim any remaining whitespace
                while (!value.empty() && (value.front() == ' ' || value.front() == '"')) {
                    value.erase(0, 1);
                }
                while (!value.empty() && (value.back() == ' ' || value.back() == '"')) {
                    value.pop_back();
                }
                return true;
            }
        }
        value = defaultVal;
        return false;
    }

    bool PenConfig::parseIntField(const std::string& obj, const char* field, int& value, int defaultVal) {
        std::string strValue;
        if (parseStringField(obj, field, strValue, "")) {
            try {
                // Remove any quotes and convert to integer
                strValue.erase(remove(strValue.begin(), strValue.end(), '"'), strValue.end());
                value = std::stoi(strValue);
                return true;
            } catch (...) {
                value = defaultVal;
            }
        }
        value = defaultVal;
        return false;
    }

    bool PenConfig::parseFloatField(const std::string& obj, const char* field, float& value, float defaultVal) {
        std::string strValue;
        if (parseStringField(obj, field, strValue, "")) {
            try {
                // Remove any quotes and convert to float
                strValue.erase(remove(strValue.begin(), strValue.end(), '"'), strValue.end());
                strValue.erase(remove(strValue.begin(), strValue.end(), '['), strValue.end());
                strValue.erase(remove(strValue.begin(), strValue.end(), ']'), strValue.end());
                value = std::stof(strValue);
                return true;
            } catch (...) {
                value = defaultVal;
            }
        }
        value = defaultVal;
        return false;
    }

    bool PenConfig::parseBoolField(const std::string& obj, const char* field, bool& value, bool defaultVal) {
        std::string strValue;
        if (parseStringField(obj, field, strValue, "false")) {
            value = (strValue == "true" || strValue == "1");
            return true;
        }
        value = defaultVal;
        return false;
    }

    bool PenConfig::loadPenConfig() {
        try {
            log_debug("Loading pen config from " << penConfigPath);
            FileStream file(penConfigPath, "r");
            std::string jsonStr;
            char buf[256];
            size_t len;
            while ((len = file.read(buf, sizeof(buf))) > 0) {
                jsonStr.append(buf, len);
            }

            log_debug("Pen config content: " << jsonStr);
            std::vector<Pen> newPens;
            
            // Parse pens array
            size_t pensStart = jsonStr.find("\"pens\":");
            if (pensStart != std::string::npos) {
                size_t arrayBegin = jsonStr.find("[", pensStart);
                size_t arrayEnd = jsonStr.find("]", arrayBegin);
                
                size_t pos = arrayBegin;
                while ((pos = jsonStr.find("{", pos)) != std::string::npos && pos < arrayEnd) {
                    size_t end = jsonStr.find("}", pos);
                    std::string obj = jsonStr.substr(pos, end - pos + 1);
                    
                    Pen pen;
                    if (parseStringField(obj, "name", pen.name) &&
                        parseStringField(obj, "color", pen.color)) {
                        parseIntField(obj, "zValue", pen.zValue, -25);
                        parseStringField(obj, "penPick", pen.penPick, "");
                        parseStringField(obj, "penDrop", pen.penDrop, "");
                        parseBoolField(obj, "skipped", pen.skipped, false);

                        if (!pen.name.empty()) {
                            newPens.push_back(pen);
                        }
                    }
                    pos = end + 1;
                }
            }

            if (!newPens.empty()) {
                pens = std::move(newPens);
                penConfigLoaded = true;
                log_info("Loaded " << pens.size() << " pens");
                return true;
            }
            return false;
        } catch (const Error&) {  // Remove err variable since we can't log it
            log_error("Failed to load pen config file");
            penConfigLoaded = false;
            return false;
        }
    }

    bool PenConfig::loadToolConfig() {
        try {
            log_debug("Loading tool config from " << toolConfigPath);
            FileStream file(toolConfigPath, "r");
            std::string jsonStr;
            char buf[256];
            size_t len;
            while ((len = file.read(buf, sizeof(buf))) > 0) {
                jsonStr.append(buf, len);
            }

            log_debug("Tool config content: " << jsonStr);
            std::vector<PenChangeConfig> newConfigs;
            
            // Parse configs array
            size_t configsStart = jsonStr.find("\"configs\":");
            if (configsStart != std::string::npos) {
                size_t arrayBegin = jsonStr.find("[", configsStart);
                size_t arrayEnd = jsonStr.find("]", arrayBegin);
                
                size_t pos = arrayBegin;
                while ((pos = jsonStr.find("{", pos)) != std::string::npos && pos < arrayEnd) {
                    size_t end = jsonStr.find("}", pos);
                    std::string obj = jsonStr.substr(pos, end - pos + 1);
                    
                    PenChangeConfig cfg;
                    if (parseStringField(obj, "PenGcode", cfg.PenGcode)) {
                        parseFloatField(obj, "x", cfg.x, 0.0f);
                        parseFloatField(obj, "y", cfg.y, 0.0f);
                        parseFloatField(obj, "z", cfg.z, 0.0f);
                        parseBoolField(obj, "occupied", cfg.occupied, false);

                        if (!cfg.PenGcode.empty()) {
                            newConfigs.push_back(cfg);
                        }
                    }
                    pos = end + 1;
                }
            }

            // Parse skipped
            size_t skippedPos = obj.find("\"skipped\":");
            if (skippedPos != std::string::npos) {
                std::string skippedStr = obj.substr(skippedPos + 9, 5);
                pen.skipped = (skippedStr.find("true") != std::string::npos);
            }

            log_debug("Adding pen: " << pen.name << " color=" << pen.color);  // Updated debug logging
            pens.push_back(pen);
            pos = end + 1;
        }

        return true;
    }

}  // namespace WebUI