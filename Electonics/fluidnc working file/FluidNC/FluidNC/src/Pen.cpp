#include "pen.h"
#include "Planner.h" // Add this include here instead of in pen.h

std::array<PenPosition, MAX_PENS> pen_positions;
int current_pen = 0;

void initialize_pen_positions() {
    // Initialize hardcoded pen positions in millimeters
    // Format: X, Y, Z, occupied
    pen_positions[0] = { 10.0f, 0.0f, 0.0f, true };    // Pen 1 at 10mm in X
    pen_positions[1] = { 30.0f, 0.0f, 0.0f, true };    // Pen 2 at 30mm in X
    pen_positions[2] = { 50.0f, 0.0f, 0.0f, true };    // Pen 3
    pen_positions[3] = { 70.0f, 0.0f, 0.0f, true };    // Pen 4
    pen_positions[4] = { 90.0f, 0.0f, 0.0f, true };    // Pen 5
    pen_positions[5] = { 110.0f, 0.0f, 0.0f, true };   // Pen 6
    pen_positions[6] = { 130.0f, 0.0f, 0.0f, true };   // Pen 7
}

bool set_current_pen(int pen_number) {
    if (pen_number >= 0 && pen_number <= MAX_PENS) {
        current_pen = pen_number;
        return true;
    }
    return false;
}

void get_pen_pickup_position(int pen_number, float position[3]) {
    if (pen_number > 0 && pen_number <= MAX_PENS) {
        auto& pen = pen_positions[pen_number - 1];
        position[0] = pen.x;
        position[1] = pen.y;
        position[2] = pen.z;
    }
}

void get_pen_place_position(int pen_number, float position[3]) {
    if (pen_number > 0 && pen_number <= MAX_PENS) {
        auto& pen = pen_positions[pen_number - 1];
        position[0] = pen.x;
        position[1] = pen.y;
        position[2] = pen.z;
    }
}
