#pragma once

#include <string>
#include <vector>
#include "../Config.h"
#include "JSONEncoder.h"

namespace WebUI {
    struct Tool {
        int number;
        float x;
        float y;
        float z;
        bool occupied;
    };

    struct ToolStatus {
        int currentPen;
        int totalPens;
        std::vector<bool> penStatus;
        bool inMotion;
        bool error;
        std::string lastError;
    };

    class ToolConfig {
    public:
        ToolConfig() {
            loadConfig();  // Load configuration on construction
        }

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
        
        bool getToolPosition(int toolNumber, float* position);
        bool isToolOccupied(int toolNumber);
        void setToolOccupied(int toolNumber, bool state);
        bool saveCurrentState(int currentPen);
        int getLastKnownState();
        bool checkCollisionRisk(int fromPen, int toPen);

        bool validatePosition(const Tool& pos);  // Changed to use Tool instead of ToolPosition
        ToolStatus getStatus();
        void reportStatus();

        bool parseJsonNumber(const std::string& json, const char* key, int& value);
        bool parseJsonFloat(const std::string& json, const char* key, float& value);

    private:
        std::vector<Tool> tools;
        const char* configPath = "/sd/config/toolconfig.json";  // Updated path
        const char* stateFile = "/sd/config/penstate.json";    // Updated path

        static constexpr float TOOL_X_POSITION = -496.0f;   // All tools are at this X position
        static constexpr float TOOL_Y_SPACING = -41.2f;     // Approximate spacing between tools
        static constexpr float TOOL_Z_LEVEL = -20.0f;       // Standard Z position
        static constexpr int MAX_TOOLS = 6;                 // Maximum number of tools
    };
}
