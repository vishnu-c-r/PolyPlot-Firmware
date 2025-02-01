#pragma once

#include <string>
#include <vector>
#include "../Config.h"
#include "JSONEncoder.h"

namespace WebUI {
    struct Pen {
        std::string color;
        std::string name;
        int         zValue;
        std::vector<std::string> penPick;
        std::vector<std::string> penDrop;
        bool        skipped;
    };

    class PenConfig {
    public:
        static PenConfig& getInstance() {
            static PenConfig instance;
            return instance;
        }

        bool             loadConfig();
        bool             saveConfig();
        bool             addPen(const Pen& pen);
        bool             updatePen(const Pen& pen);
        bool             deletePen(const std::string& name);  // Changed to use name instead of ID
        std::vector<Pen> getPens() { return pens; }
        std::string      toJSON();
        bool             fromJSON(const std::string& jsonStr);

    private:
        PenConfig() {}  // Private constructor for singleton
        std::vector<Pen> pens;
        const char*      configPath = "/localfs/penconfig.json";
    };
}
