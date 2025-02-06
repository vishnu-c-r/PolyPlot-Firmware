#include "PenConfig.h"
#include "../FileStream.h"
#include "JSONEncoder.h"
#include "Driver/localfs.h"
#include <string>
#include <filesystem>

namespace WebUI {

    bool PenConfig::loadConfig() {
        try {
            std::error_code ec;
            
            // Create config directory if it doesn't exist
            if (!std::filesystem::exists("/sd/config", ec)) {
                std::filesystem::create_directory("/sd/config", ec);
            }
            
            // If file doesn't exist, just initialize empty pen list without creating file
            if (!std::filesystem::exists(configPath, ec)) {
                pens.clear();
                return true;
            }

            FileStream file(configPath, "r");
            if (!file) {
                log_error("Failed to open pen configuration file");
                return false;
            }

            std::string jsonStr;
            char buf[64];
            size_t len;

            while ((len = file.read(buf, sizeof(buf))) > 0) {
                jsonStr.append(buf, len);
            }

            if (jsonStr.empty()) {
                return true;  // Empty file is valid - just means no pens configured
            }

            return fromJSON(jsonStr);
        } catch (const Error& err) {
            log_error("Error loading pen config: " << (int)err);
            return false;
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
            j.member("zValue", pen.zValue);  // Changed: Remove string conversion
            
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
        try {
            std::vector<Pen> tempPens;  // Temporary storage for validation

            // Verify JSON starts with opening brace
            if (jsonStr.find("{") == std::string::npos) {
                log_error("Invalid JSON format - missing opening brace");
                return false;
            }

            size_t arrayStart = jsonStr.find("[");
            if (arrayStart == std::string::npos) {
                log_info("No pens array found in JSON - starting fresh");
                pens.clear();
                return true;
            }

            size_t pos = arrayStart;
            bool hadError = false;

            while ((pos = jsonStr.find("{", pos)) != std::string::npos) {
                size_t end = jsonStr.find("}", pos);
                if (end == std::string::npos) break;

                std::string obj = jsonStr.substr(pos, end - pos + 1);
                
                try {
                    Pen pen;
                    if (!parseJsonString(obj, "name", pen.name) || 
                        !parseJsonString(obj, "color", pen.color) ||
                        !parseJsonInt(obj, "zValue", pen.zValue)) {
                        log_warn("Skipping malformed pen entry");
                        hadError = true;
                        continue;
                    }

                    if (!parseJsonStringArray(obj, "penPick", pen.penPick) ||
                        !parseJsonStringArray(obj, "penDrop", pen.penDrop)) {
                        log_warn("Skipping pen with invalid pick/drop commands");
                        hadError = true;
                        continue;
                    }

                    pen.skipped = parseJsonBool(obj, "skipped", false);
                    tempPens.push_back(pen);

                } catch (const std::exception& e) {
                    log_error("Exception parsing pen: " << e.what());
                    hadError = true;
                }

                pos = end + 1;
            }

            // Only update if we got at least one valid pen or the file was intentionally empty
            if (!tempPens.empty() || jsonStr.find("\"pens\":[]") != std::string::npos) {
                pens = std::move(tempPens);
                return true;
            }

            if (hadError) {
                log_error("No valid pens found in configuration");
                return false;
            }

            return true;

        } catch (const std::exception& e) {
            log_error("Exception in fromJSON: " << e.what());
            return false;
        }
    }

    // Make these member functions of PenConfig
    bool PenConfig::parseJsonString(const std::string& json, const char* key, std::string& value) {
        std::string keyStr = "\"" + std::string(key) + "\":";
        size_t pos = json.find(keyStr);
        if (pos == std::string::npos) return false;

        size_t valueStart = json.find("\"", pos + keyStr.length());
        if (valueStart == std::string::npos) return false;

        size_t valueEnd = json.find("\"", valueStart + 1);
        if (valueEnd == std::string::npos) return false;

        value = json.substr(valueStart + 1, valueEnd - valueStart - 1);
        return true;
    }

    bool PenConfig::parseJsonInt(const std::string& json, const char* key, int& value) {
        std::string keyStr = "\"" + std::string(key) + "\":";
        size_t pos = json.find(keyStr);
        if (pos == std::string::npos) return false;

        pos += keyStr.length();
        while (pos < json.length() && std::isspace(json[pos])) pos++;

        size_t valueEnd = pos;
        while (valueEnd < json.length() && 
               (std::isdigit(json[valueEnd]) || json[valueEnd] == '-')) {
            valueEnd++;
        }

        try {
            value = std::stoi(json.substr(pos, valueEnd - pos));
            return true;
        } catch (...) {
            return false;
        }
    }

    bool PenConfig::parseJsonBool(const std::string& json, const char* key, bool defaultValue) {
        std::string keyStr = "\"" + std::string(key) + "\":";
        size_t pos = json.find(keyStr);
        if (pos == std::string::npos) return defaultValue;

        pos += keyStr.length();
        while (pos < json.length() && std::isspace(json[pos])) pos++;

        return json.substr(pos, 4) == "true";
    }

    bool PenConfig::parseJsonStringArray(const std::string& json, const char* key, std::vector<std::string>& array) {
        array.clear();
        std::string keyStr = "\"" + std::string(key) + "\":";
        size_t pos = json.find(keyStr);
        if (pos == std::string::npos) return false;

        size_t arrayStart = json.find("[", pos);
        size_t arrayEnd = json.find("]", pos);
        if (arrayStart == std::string::npos || arrayEnd == std::string::npos) return false;

        std::string arrayStr = json.substr(arrayStart + 1, arrayEnd - arrayStart - 1);
        size_t strStart = 0;
        
        while ((strStart = arrayStr.find("\"", strStart)) != std::string::npos) {
            size_t strEnd = arrayStr.find("\"", strStart + 1);
            if (strEnd == std::string::npos) break;
            array.push_back(arrayStr.substr(strStart + 1, strEnd - strStart - 1));
            strStart = strEnd + 1;
        }

        return true;
    }

}  // namespace WebUI