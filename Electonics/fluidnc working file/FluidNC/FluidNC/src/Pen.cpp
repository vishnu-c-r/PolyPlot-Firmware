#include "Pen.h"
#include "GCode.h"  // For MAX_PENS

static PenLocation penLocations[] = {
    { -494.700f, 39.9f, -14.0f, false },  // Pen 1 location (Z moves down from 0)
    { -100.0f, 85.0f, -15.0f, false },  // Pen 2 location
    { -100.0f, 95.0f, -15.0f, false },  // Pen 3 location
    { -492.0f, 162.30f, -14.0f, false }, // Pen 4 location
    { -100.0f, 115.0f, -15.0f, false }, // Pen 5 location 
    { -100.0f, 125.0f, -15.0f, false }, // Pen 6 location
    { -100.0f, 135.0f, -15.0f, false }, // Pen 7 location
    { -100.0f, 145.0f, -15.0f, false }  // Pen 8 location
};

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
    PenLocation loc = getPenLocation(penNumber - 1);
    position[X_AXIS] = loc.x;
    position[Y_AXIS] = loc.y;
    position[Z_AXIS] = loc.z;
    return true;
}

bool get_pen_pickup_position(int penNumber, float* position) {
    if (penNumber <= 0 || penNumber > MAX_PENS) {
        return false;
    }
    PenLocation loc = getPenLocation(penNumber - 1);
    position[X_AXIS] = loc.x;
    position[Y_AXIS] = loc.y;
    position[Z_AXIS] = loc.z;
    return true;
}
