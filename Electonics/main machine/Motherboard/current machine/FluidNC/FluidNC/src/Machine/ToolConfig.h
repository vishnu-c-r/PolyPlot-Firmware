#pragma once

#include <string>
#include <vector>
#include "../Config.h"
#include "../WebUI/JSONEncoder.h"

namespace Machine {
    struct Tool {
        int number;
        float x;
        float y;
        float z;
        bool occupied;
    };

    class ToolConfig {
    public:
        static ToolConfig& getInstance() {
            static ToolConfig instance;
            return instance;
        }

        bool loadConfig();
        bool saveConfig();
        bool addTool(const Tool& tool);
        bool updateTool(const Tool& tool);
        bool deleteTool(int number);
        std::vector<Tool> getTools() { return tools; }
        Tool* getTool(int number);
        std::string toJSON();
        bool fromJSON(const std::string& jsonStr);
        
        // Functions previously in Pen namespace
        bool getToolPosition(int toolNumber, float* position);
        bool isToolOccupied(int toolNumber);
        void setToolOccupied(int toolNumber, bool state);

    private:
        ToolConfig() {}  // Private constructor for singleton
        std::vector<Tool> tools;
        const char* configPath = "/localfs/toolconfig.json";
    };
}
