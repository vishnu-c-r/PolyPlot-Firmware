#pragma once

#include <string>
#include <vector>
#include "../Config.h"
#include "JSONEncoder.h"
#include "Driver/localfs.h"

namespace WebUI {
    /**
     * @brief Represents a single pen/tool configuration
     * Stores all parameters needed for pen changes and operation
     */
    struct Pen {
        std::string color;  // HTML color code for UI display
        std::string name;   // Unique identifier for the pen
        // int zValue;              // Z-axis position when pen is in use
        std::vector<std::string> penPick;   // GCode commands for picking pen from holder
        std::vector<std::string> penDrop;   // GCode commands for returning pen to holder
        int                      feedRate;  // Movement speed in mm/min when using this pen
        bool                     skipped;   // If true, pen is ignored in auto-change sequences
    };

    /**
     * @brief Manages pen configurations and persistent storage
     * Implements a singleton pattern for global access to pen settings
     */
    class PenConfig {
    public:
        // Get singleton instance
        static PenConfig& getInstance() {
            static PenConfig instance;
            return instance;
        }

        // File operations
        bool loadConfig();  // Load pen configurations from JSON file
        bool saveConfig();  // Save pen configurations to JSON file

        // Pen management
        bool             addPen(const Pen& pen);              // Add a new pen configuration
        bool             updatePen(const Pen& pen);           // Update an existing pen
        bool             deletePen(const std::string& name);  // Delete a pen configuration
        Pen*             getPen(const std::string& name);     // Get pen by name
        Pen*             getPenByIndex(size_t index);         // Get pen by index
        std::vector<Pen> getPens() { return pens; }           // Get all pens

        // JSON conversion
        std::string toJSON();                              // Convert configurations to JSON
        bool        fromJSON(const std::string& jsonStr);  // Parse configurations from JSON

    private:
        std::vector<Pen> pens;                                   // List of pen configurations
        const char*      configPath = "/spiffs/penConfig.json";  // Configuration file path

        // JSON parsing helper functions
        bool   parseJsonString(const std::string& json, const char* key, std::string& value);
        bool   parseJsonInt(const std::string& json, const char* key, int& value);
        bool   parseJsonBool(const std::string& json, const char* key, bool defaultValue);
        bool   parseJsonStringArray(const std::string& json, const char* key, std::vector<std::string>& array);
        size_t findMatchingBrace(const std::string& json, size_t startPos);
    };

}  // namespace WebUI
