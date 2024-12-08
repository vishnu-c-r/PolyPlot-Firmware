#pragma once

#include <array>
#include <cstdint>
#include "GCode.h" // Include this for MAX_PENS definition

#define MAX_PENS 8

// Forward declaration
struct plan_line_data_t;

struct PenPosition {
    float x;
    float y;
    float z;
    bool occupied; // true if pen is in holder
};

// Global pen management
extern std::array<PenPosition, MAX_PENS> pen_positions;
extern int current_pen; // 0 means no pen, 1-10 are valid pens

void initialize_pen_positions();
bool set_current_pen(int pen_number);
void get_pen_pickup_position(int pen_number, float position[3]); 
void get_pen_place_position(int pen_number, float position[3]);
bool plan_pen_change_moves(int new_pen, plan_line_data_t* pl_data, float* current_position);
bool plan_pen_move(float* target, plan_line_data_t* pl_data, float* start_pos);

// Initialize pen module
void init_pen_system();

// Set and get current pen
int get_current_pen();

// Convert laser position to pen position
void laser_to_pen_position(float* target);
