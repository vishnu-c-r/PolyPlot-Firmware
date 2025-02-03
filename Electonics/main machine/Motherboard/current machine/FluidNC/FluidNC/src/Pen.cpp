#include "Pen.h"
#include "GCode.h"
#include "WebUI/ToolConfig.h"

namespace Pen {
    void pickPen(int penIndex) {
        if (penIndex >= 0 && penIndex < MAX_PENS) {
            auto& toolConfig = WebUI::ToolConfig::getInstance();
            toolConfig.setOccupied(penIndex + 1, false);
        }
    }

    void dropPen(int penIndex) {
        if (penIndex >= 0 && penIndex < MAX_PENS) {
            auto& toolConfig = WebUI::ToolConfig::getInstance();
            toolConfig.setOccupied(penIndex + 1, true);
        }
    }
}

// Remove the static penLocations array since we're now using ToolConfig

PenLocation getPenLocation(int penIndex) {
    auto& toolConfig = WebUI::ToolConfig::getInstance();
    auto toolPos = toolConfig.getPosition(penIndex + 1);
    
    PenLocation loc;
    loc.x = toolPos.x;
    loc.y = toolPos.y;
    loc.z = toolPos.z;
    loc.occupied = toolPos.occupied;
    
    return loc;
}

void setPenOccupied(int penIndex, bool state) {
    auto& toolConfig = WebUI::ToolConfig::getInstance();
    toolConfig.setOccupied(penIndex + 1, state);
}

bool get_pen_place_position(int penNumber, float* position) {
    if (penNumber <= 0 || penNumber > MAX_PENS) {
        return false;
    }
    
    auto& toolConfig = WebUI::ToolConfig::getInstance();
    auto toolPos = toolConfig.getPosition(penNumber);
    
    position[X_AXIS] = toolPos.x;
    position[Y_AXIS] = toolPos.y;
    position[Z_AXIS] = toolPos.z;
    
    return true;
}

bool get_pen_pickup_position(int penNumber, float* position) {
    if (penNumber <= 0 || penNumber > MAX_PENS) {
        return false;
    }
    
    auto& toolConfig = WebUI::ToolConfig::getInstance();
    auto toolPos = toolConfig.getPosition(penNumber);
    
    position[X_AXIS] = toolPos.x;
    position[Y_AXIS] = toolPos.y;
    position[Z_AXIS] = toolPos.z;
    
    return true;
}
