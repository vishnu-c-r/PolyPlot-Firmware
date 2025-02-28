#pragma once

#include <string>
#include <vector>
#include "../Config.h"
#include "JSONEncoder.h"
#include "Driver/localfs.h"  // Add this include for localfs_exists

namespace WebUI {
    struct Pen {
        std::string color;
        std::string name;
        int zValue;
        std::vector<std::string> penPick;
        std::vector<std::string> penDrop;
        int feedRate;
        bool skipped;
    };

    class PenConfig {
    public:
        static PenConfig& getInstance() {
            static PenConfig instance;
            return instance;
        }

        bool loadConfig();
        bool saveConfig();
        bool addPen(const Pen& pen);
        bool updatePen(const Pen& pen);
        bool deletePen(const std::string& name);
        std::vector<Pen> getPens() { return pens; }
        std::string toJSON();
        bool fromJSON(const std::string& jsonStr);

    private:
        std::vector<Pen> pens;
        const char* configPath = "/spiffs/penConfig.json";  // Updated path

        bool parseJsonString(const std::string& json, const char* key, std::string& value);
        bool parseJsonInt(const std::string& json, const char* key, int& value);
        bool parseJsonBool(const std::string& json, const char* key, bool defaultValue);
        bool parseJsonStringArray(const std::string& json, const char* key, std::vector<std::string>& array);
    };

}  // namespace WebUI
