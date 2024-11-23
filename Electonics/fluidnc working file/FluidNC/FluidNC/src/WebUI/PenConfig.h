#pragma once

#include <string>
#include <vector>
#include "../Config.h"
#include "JSONEncoder.h"

namespace WebUI {
    struct Pen {
        int         id;
        std::string color;
        int         order;
        // Add any other pen properties you need
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
        bool             deletePen(int id);
        std::vector<Pen> getPens();
        std::string      toJSON();
        bool             fromJSON(const std::string& jsonStr);

    private:
        PenConfig() {}  // Private constructor for singleton
        std::vector<Pen> pens;
        const char*      configPath = "/localfs/penconfig.json";
    };
}
