#pragma once

#include <cstdint>

// Remove MAX_PENS define since it's defined in GCode.h
// #define MAX_PENS 8

struct PenLocation {
    float x;
    float y; 
    float z;
    bool occupied;
};

// Function declarations
PenLocation getPenLocation(int penIndex);
void setPenOccupied(int penIndex, bool state);
bool get_pen_pickup_position(int pen_number, float position[3]);
bool get_pen_place_position(int pen_number, float position[3]);

namespace Pen {
    void pickPen(int penIndex);
    void dropPen(int penIndex);
}

// Current pen tracking
extern int current_pen; // 0 means no pen, 1-MAX_PENS are valid pens

// Include this file in any place where you need to access pen_change

// Use the global declaration from GCode.h
extern volatile bool pen_change;  // Flag to indicate active tool change
extern volatile bool cycle_start_tool_change;  // Flag to force cycle start on tool change

// Add any pen-specific functions or constants here
