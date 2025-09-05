#pragma once

#include <string>
#include <vector>
#include "../Config.h"
#include "JSONEncoder.h"

namespace WebUI {
    struct Tool {
        int   number;
        float x;
        float y;
        float z;
        bool  occupied;
    };

    struct ToolStatus {
        int               currentPen;
        int               totalPens;
        std::vector<bool> penStatus;
        bool              inMotion;
        bool              error;
        std::string       lastError;
    };

    class ToolConfig {
    public:
        ToolConfig() {
            if (!loadConfig()) {
                log_error("Failed to load tool configuration");
            }
        }

        static ToolConfig& getInstance() {
            static ToolConfig instance;
            return instance;
        }

        bool              loadConfig();
        bool              saveConfig();
        bool              addTool(const Tool& tool);
        bool              updateTool(const Tool& tool);
        bool              deleteTool(int number);
        std::vector<Tool> getTools() { return tools; }
        Tool*             getTool(int number);
        void              sortByNumber();
        std::string       toJSON();
        bool              fromJSON(const std::string& jsonStr);

        bool getToolPosition(int toolNumber, float* position);
        bool isToolOccupied(int toolNumber);
        void setToolOccupied(int toolNumber, bool state);
        bool saveCurrentState(int currentPen);
        int  getLastKnownState();
        bool checkCollisionRisk(int fromPen, int toPen);

        bool       validatePosition(const Tool& pos);
        ToolStatus getStatus();
        void       reportStatus();

        bool parseJsonNumber(const std::string& json, const char* key, int& value);
        bool parseJsonFloat(const std::string& json, const char* key, float& value);
        bool ensureLoaded();  // Moved to public section

    private:
        bool                 configLoaded = false;
        bool                 cacheValid   = false;
        std::vector<Tool>    tools;
        std::string          cachedJson;
        const char*          configPath = "/spiffs/toolconfig.json";
        const char*          stateFile  = "/spiffs/penstate.json";
        static constexpr int MAX_TOOLS  = 6;
    };
}
