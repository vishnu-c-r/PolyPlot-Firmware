#pragma once

#include "Config.h"  // Include necessary config headers

#define MAX_PENS 10  // Define the maximum number of pens

struct PenPosition {
    float x;  // X coordinate
    float y;  // Y coordinate
    float z;  // Z coordinate
};

// Array to store the positions of the pens
static PenPosition pen_positions[MAX_PENS] = {
    {100.0, 0.0, 0.0},    // Pen 1 position
    {120.0, 0.0, 0.0},    // Pen 2 position
    {140.0, 0.0, 0.0},    // Pen 3 position
    {160.0, 0.0, 0.0},    // Pen 4 position
    {180.0, 0.0, 0.0},    // Pen 5 position
    {200.0, 0.0, 0.0},    // Pen 6 position
    {220.0, 0.0, 0.0},    // Pen 7 position
    {240.0, 0.0, 0.0},    // Pen 8 position
    {260.0, 0.0, 0.0},    // Pen 9 position
    {280.0, 0.0, 0.0},    // Pen 10 position
};

// Function to get the XYZ position of a pen
const PenPosition* get_pen_position(uint8_t pen_number) {
    if (pen_number >= 1 && pen_number <= MAX_PENS) {
        return &pen_positions[pen_number - 1];  // Return the position of the requested pen
    }
    return nullptr;  // Return null if the pen number is invalid
}