#include "PenConfig.h"
#include "../FileStream.h"
#include "JSONEncoder.h"
#include "Driver/localfs.h"
#include <string>
#include <filesystem>

namespace WebUI {

    // Load the pen configuration from file.
    bool PenConfig::loadConfig() {
        try {
            FileStream file(configPath, "r");
            if (!file) {
                log_error("Failed to open pen config file");
                return false;
            }
            std::string jsonStr;
            char buf[256];
            size_t len;
            while ((len = file.read(buf, sizeof(buf))) > 0)
                jsonStr.append(buf, len);
            if (jsonStr.empty()) {
                log_error("Empty pen config file");
                return false;
            }
            return fromJSON(jsonStr);
        } catch (const Error& err) {
            log_error("Error loading pen config: " << (int)err);
            return false;
        }
    }

    // Save the current pen configuration to file.
    bool PenConfig::saveConfig() {
        try {
            {  // Automatic file closure.
                FileStream file(configPath, "w");
                std::string jsonStr = toJSON();
                file.write(reinterpret_cast<const uint8_t*>(jsonStr.c_str()), jsonStr.length());
            }
            return true;
        } catch (const Error err) {
            log_error("Failed to save pen config");
            return false;
        }
    }

    // Convert pen configuration to a JSON string.
    std::string PenConfig::toJSON() {
        std::string output;
        JSONencoder j(&output);
        j.begin();
        j.begin_array("pens");
        for (const auto& pen : pens) {
            j.begin_object();
            j.member("name", pen.name.c_str());
            j.member("color", pen.color.c_str());
            j.member("zValue", std::to_string(pen.zValue).c_str());
            j.member("feedRate", std::to_string(pen.feedRate).c_str());
            j.begin_array("penPick");
            for (const auto& pick : pen.penPick)
                j.string(pick.c_str());
            j.end_array();
            j.begin_array("penDrop");
            for (const auto& drop : pen.penDrop)
                j.string(drop.c_str());
            j.end_array();
            j.member("skipped", pen.skipped ? "true" : "false");
            j.end_object();
        }
        j.end_array();
        j.end();
        return output;
    }

    // Parse the provided JSON string to update the pen list.
    bool PenConfig::fromJSON(const std::string& jsonStr) {
        pens.clear();
        size_t arrayStart = jsonStr.find("[");
        if (arrayStart == std::string::npos) {
            log_error("No pens array found in JSON");
            return false;
        }
        size_t pos = arrayStart;
        while ((pos = jsonStr.find("{", pos)) != std::string::npos) {
            size_t end = jsonStr.find("}", pos);
            if (end == std::string::npos)
                break;
            std::string obj = jsonStr.substr(pos, end - pos + 1);
            Pen pen;
            bool validPen = true;
            if (!parseJsonString(obj, "name", pen.name))
                validPen = false;
            if (!parseJsonString(obj, "color", pen.color))
                validPen = false;
            if (!parseJsonInt(obj, "zValue", pen.zValue))
                validPen = false;
            if (!parseJsonInt(obj, "feedRate", pen.feedRate))
                validPen = false;
            if (!parseJsonStringArray(obj, "penPick", pen.penPick))
                validPen = false;
            if (!parseJsonStringArray(obj, "penDrop", pen.penDrop))
                validPen = false;
            pen.skipped = parseJsonBool(obj, "skipped", false);
            if (validPen)
                pens.push_back(pen);
            else
                log_error("Invalid pen data in JSON object");
            pos = end + 1;
        }
        return !pens.empty();
    }

    // Parse a string value for the given key from JSON.
    bool PenConfig::parseJsonString(const std::string& json, const char* key, std::string& value) {
        std::string keyStr = "\"" + std::string(key) + "\":";
        size_t pos = json.find(keyStr);
        if (pos == std::string::npos)
            return false;
        size_t valueStart = json.find("\"", pos + keyStr.length());
        if (valueStart == std::string::npos)
            return false;
        size_t valueEnd = json.find("\"", valueStart + 1);
        if (valueEnd == std::string::npos)
            return false;
        value = json.substr(valueStart + 1, valueEnd - valueStart - 1);
        return true;
    }

    // Parse an integer value for the given key from JSON.
    bool PenConfig::parseJsonInt(const std::string& json, const char* key, int& value) {
        std::string keyStr = "\"" + std::string(key) + "\":";
        size_t pos = json.find(keyStr);
        if (pos == std::string::npos)
            return false;
        pos += keyStr.length();
        while (pos < json.length() && std::isspace(json[pos]))
            pos++;
        size_t valueEnd = pos;
        while (valueEnd < json.length() && (std::isdigit(json[valueEnd]) || json[valueEnd] == '-'))
            valueEnd++;
        try {
            value = std::stoi(json.substr(pos, valueEnd - pos));
            return true;
        } catch (...) {
            return false;
        }
    }

    // Parse a boolean value for the given key from JSON.
    bool PenConfig::parseJsonBool(const std::string& json, const char* key, bool defaultValue) {
        std::string keyStr = "\"" + std::string(key) + "\":";
        size_t pos = json.find(keyStr);
        if (pos == std::string::npos)
            return defaultValue;
        pos += keyStr.length();
        while (pos < json.length() && std::isspace(json[pos]))
            pos++;
        return json.substr(pos, 4) == "true";
    }

    // Parse an array of strings for the given key from JSON.
    bool PenConfig::parseJsonStringArray(const std::string& json, const char* key, std::vector<std::string>& array) {
        array.clear();
        std::string keyStr = "\"" + std::string(key) + "\":";
        size_t pos = json.find(keyStr);
        if (pos == std::string::npos)
            return false;
        size_t arrayStart = json.find("[", pos);
        size_t arrayEnd = json.find("]", pos);
        if (arrayStart == std::string::npos || arrayEnd == std::string::npos)
            return false;
        std::string arrayStr = json.substr(arrayStart + 1, arrayEnd - arrayStart - 1);
        size_t strStart = 0;
        while ((strStart = arrayStr.find("\"", strStart)) != std::string::npos) {
            size_t strEnd = arrayStr.find("\"", strStart + 1);
            if (strEnd == std::string::npos)
                break;
            array.push_back(arrayStr.substr(strStart + 1, strEnd - strStart - 1));
            strStart = strEnd + 1;
        }
        return true;
    }

    // Add a new pen configuration.
    bool PenConfig::addPen(const Pen& pen) {
        for (const auto& existing : pens) {
            if (existing.name == pen.name)
                return false;
        }
        pens.push_back(pen);
        return true;
    }
    
    // Update an existing pen configuration.
    bool PenConfig::updatePen(const Pen& pen) {
        for (auto& existing : pens) {
            if (existing.name == pen.name) {
                existing = pen;
                return true;
            }
        }
        return false;
    }
    
    // Remove a pen configuration by name.
    bool PenConfig::deletePen(const std::string& name) {
        for (auto it = pens.begin(); it != pens.end(); ++it) {
            if (it->name == name) {
                pens.erase(it);
                return true;
            }
        }
        return false;
    }

}  // namespace WebUI