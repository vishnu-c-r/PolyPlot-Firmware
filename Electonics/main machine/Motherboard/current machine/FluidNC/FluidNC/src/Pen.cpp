#include "Pen.h"
#include "GCode.h"  // For MAX_PENS

static PenLocation penLocations[] = {};

PenLocation getPenLocation(int penIndex) {
    if (penIndex >= 0 && penIndex < MAX_PENS) {
        return penLocations[penIndex];
    }
    // Return first pen location as fallback
    return penLocations[0];
}

void setPenOccupied(int penIndex, bool state) {
    if (penIndex >= 0 && penIndex < MAX_PENS) {
        penLocations[penIndex].occupied = state;
    }
}

// Add namespace Pen implementation
namespace Pen {
    void pickPen(int penIndex) {
        if (penIndex >= 0 && penIndex < MAX_PENS) {
            // Mark the pen as no longer in the holder
            setPenOccupied(penIndex, false);
        }
    }

    void dropPen(int penIndex) {
        if (penIndex >= 0 && penIndex < MAX_PENS) {
            // Mark the pen as returned to the holder
            setPenOccupied(penIndex, true);
        }
    }
}

bool get_pen_place_position(int penNumber, float* position) {
    if (penNumber <= 0 || penNumber > MAX_PENS) {
        return false;
    }
    PenLocation loc  = getPenLocation(penNumber - 1);
    position[X_AXIS] = loc.x;
    position[Y_AXIS] = loc.y;
    position[Z_AXIS] = loc.z;
    return true;
}

bool get_pen_pickup_position(int penNumber, float* position) {
    if (penNumber <= 0 || penNumber > MAX_PENS) {
        return false;
    }
    PenLocation loc  = getPenLocation(penNumber - 1);
    position[X_AXIS] = loc.x;
    position[Y_AXIS] = loc.y;
    position[Z_AXIS] = loc.z;
    return true;
}
