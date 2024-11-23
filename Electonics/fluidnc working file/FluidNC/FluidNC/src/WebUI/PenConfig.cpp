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
        // Check if pen with same ID already exists
        for (const auto& existingPen : pens) {
            if (existingPen.id == pen.id) {
                return false;
            }
        }
        pens.push_back(pen);
        return true;
    }

    bool PenConfig::updatePen(const Pen& pen) {
        for (auto& existingPen : pens) {
            if (existingPen.id == pen.id) {
                existingPen = pen;
                return true;
            }
        }
        return false;
    }

    bool PenConfig::deletePen(int id) {
        for (auto it = pens.begin(); it != pens.end(); ++it) {
            if (it->id == id) {
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
            j.member("id", std::to_string(pen.id).c_str());
            j.member("color", pen.color.c_str());
            j.member("order", std::to_string(pen.order).c_str());
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

            Pen    pen;
            size_t idPos    = obj.find("\"id\":");
            size_t colorPos = obj.find("\"color\":");
            size_t orderPos = obj.find("\"order\":");

            if (idPos != std::string::npos) {
                pen.id = atoi(obj.substr(idPos + 5).c_str());
            }
            if (colorPos != std::string::npos) {
                size_t start = obj.find("\"", colorPos + 8) + 1;
                size_t end   = obj.find("\"", start);
                pen.color    = obj.substr(start, end - start);
            }
            if (orderPos != std::string::npos) {
                pen.order = atoi(obj.substr(orderPos + 8).c_str());
            }

            log_debug("Adding pen: id=" << pen.id << " color=" << pen.color << " order=" << pen.order);  // Add debug logging
            pens.push_back(pen);
            pos = end + 1;
        }

        return true;
    }

}  // namespace WebUI