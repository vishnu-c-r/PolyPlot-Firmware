#pragma once

#include <string>
#include <vector>
#include "../Config.h"
#include "JSONEncoder.h"

namespace WebUI {
    struct ToolPosition {
        int number;
        float x;
        float y;
        float z;
        bool occupied;
    };

    struct ToolStatus {
        int currentPen;
        int totalPens;
        std::vector<bool> penStatus;  // occupied status of all pens
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
        bool updatePosition(const ToolPosition& position);
        ToolPosition getPosition(int toolNumber);
        bool isOccupied(int toolNumber);
        void setOccupied(int toolNumber, bool state);
        std::string toJSON();
        bool fromJSON(const std::string& jsonStr);
        
        // Add these new methods
        bool saveCurrentState(int currentPen);
        int getLastKnownState();
        bool checkCollisionRisk(int fromPen, int toPen);

        bool validatePosition(const ToolPosition& pos);
        ToolStatus getStatus();
        void reportStatus();  // Send status to serial/web interface

    private:
        std::vector<ToolPosition> positions;
        const char* configPath = "/localfs/toolconfig.json";
        const char* stateFile = "/localfs/penstate.json";

        // Add these helper function declarations
        bool parseJsonNumber(const std::string& json, const char* key, int& value);
        bool parseJsonFloat(const std::string& json, const char* key, float& value);
    };
}
