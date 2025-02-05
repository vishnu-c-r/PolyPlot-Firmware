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

            // Parse name
            size_t namePos = obj.find("\"name\":");
            if (namePos != std::string::npos) {
                size_t start = obj.find("\"", namePos + 7) + 1;
                size_t end   = obj.find("\"", start);
                pen.name     = obj.substr(start, end - start);
            }

            // Parse zValue
            size_t zValuePos = obj.find("\"zValue\":");
            if (zValuePos != std::string::npos) {
                size_t valueStart = zValuePos + 9;
                while (valueStart < obj.length() && (obj[valueStart] == ' ' || obj[valueStart] == ':')) {
                    valueStart++;
                }
                size_t valueEnd = valueStart;
                while (valueEnd < obj.length() && (std::isdigit(obj[valueEnd]) || obj[valueEnd] == '-')) {
                    valueEnd++;
                }
                std::string zValueStr = obj.substr(valueStart, valueEnd - valueStart);
                pen.zValue = std::stoi(zValueStr);
            }

            // Parse penPick array
            size_t pickPos = obj.find("\"penPick\":");
            if (pickPos != std::string::npos) {
                size_t arrayStart = obj.find("[", pickPos);
                size_t arrayEnd = obj.find("]", pickPos);
                std::string arrayStr = obj.substr(arrayStart + 1, arrayEnd - arrayStart - 1);
                
                // Parse each string in the array
                size_t strStart = 0;
                while ((strStart = arrayStr.find("\"", strStart)) != std::string::npos) {
                    size_t strEnd = arrayStr.find("\"", strStart + 1);
                    if (strEnd == std::string::npos) break;
                    pen.penPick.push_back(arrayStr.substr(strStart + 1, strEnd - strStart - 1));
                    strStart = strEnd + 1;
                }
            }

            // Parse penDrop array
            size_t dropPos = obj.find("\"penDrop\":");
            if (dropPos != std::string::npos) {
                size_t arrayStart = obj.find("[", dropPos);
                size_t arrayEnd = obj.find("]", dropPos);
                std::string arrayStr = obj.substr(arrayStart + 1, arrayEnd - arrayStart - 1);
                
                // Parse each string in the array
                size_t strStart = 0;
                while ((strStart = arrayStr.find("\"", strStart)) != std::string::npos) {
                    size_t strEnd = arrayStr.find("\"", strStart + 1);
                    if (strEnd == std::string::npos) break;
                    pen.penDrop.push_back(arrayStr.substr(strStart + 1, strEnd - strStart - 1));
                    strStart = strEnd + 1;
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