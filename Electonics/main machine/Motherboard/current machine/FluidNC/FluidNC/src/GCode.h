// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2009-2011 Simen Svale Skogsrud
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Config.h"
#include "Error.h"

#include <cstdint>

typedef uint16_t gcodenum_t;

enum class Override : uint8_t {
    ParkingMotion = 0,  // M56 (Default: Must be zero)
    Disabled      = 0,  // Parking disabled.
};

// Modal group internal numbers for checking multiple command violations and tracking the
// type of command that is called in the block. A modal group is a group of g-code commands that are
// mutually exclusive, or cannot exist on the same line, because they each toggle a state or execute
// a unique motion. These are defined in the NIST RS274-NGC v3 g-code standard, available online,
// and are similar/identical to other g-code interpreters by manufacturers (Haas,Fanuc,Mazak,etc).
// NOTE: Modal group values must be sequential and starting from zero.

enum class ModalGroup : uint8_t {
    MG0  = 0,   // [G4,G10,G28,G28.1,G30,G30.1,G53,G92,G92.1] Non-modal
    MG1  = 1,   // [G0,G1,G2,G3,G38.2,G38.3,G38.4,G38.5,G80] Motion
    MG2  = 2,   // [G17,G18,G19] Plane selection
    MG3  = 3,   // [G90,G91] Distance mode
    MG4  = 4,   // [G91.1] Arc IJK distance mode
    MG5  = 5,   // [G93,G94] Feed rate mode
    MG6  = 6,   // [G20,G21] Units
    MG7  = 7,   // [G40] Cutter radius compensation mode. G41/42 NOT SUPPORTED.
    MG8  = 8,   // [G43.1,G49] Tool length offset
    MG12 = 9,   // [G54,G55,G56,G57,G58,G59] Coordinate system selection
    MG13 = 10,  // [G61] Control mode
    MM4  = 11,  // [M0,M1,M2,M30] Stopping
    MM6  = 14,  // [M6] Tool change
    MM7  = 12,
    MM8  = 13,  // [M7,M8,M9] Coolant control
    MM9  = 14,  // [M56] Override control
    MM10 = 15,  // [M62, M63, M64, M65, M67, M68] User Defined http://linuxcnc.org/docs/html/gcode/overview.html#_modal_groups
    MG9  = 16,  // [G6.1, G6.2, G6.3, G6.4, G6.5, G6.6, G6.7, G6.8, G6.9] Module
};

// Command actions for within execution-type modal groups (motion, stopping, non-modal). Used
// internally by the parser to know which command to execute.
// NOTE: Some macro values are assigned specific values to make g-code state reporting and parsing
// compile a litte smaller. Necessary due to being completely out of flash on the 328p. Although not
// ideal, just be careful with values that state 'do not alter' and check both report.c and gcode.c
// to see how they are used, if you need to alter them.

// Modal Group G0: Non-modal actions

enum class NonModal : gcodenum_t {
    NoAction              = 0,    // Default
    Dwell                 = 40,   // G4
    SetCoordinateData     = 100,  // G10
    GoHome0               = 280,  // G28
    SetHome0              = 281,  // G28.1
    GoHome1               = 300,  // G30
    SetHome1              = 301,  // G30.1
    AbsoluteOverride      = 530,  // G53
    SetCoordinateOffset   = 920,  // G92
    ResetCoordinateOffset = 921,  // G92.1
};

// Modal Group G1: Motion modes
enum class Motion : gcodenum_t {
    Seek               = 00,   // G0 Default
    Linear             = 10,   // G1
    CwArc              = 20,   // G2
    CcwArc             = 30,   // G3
    ProbeToward        = 382,  // G38.2
    ProbeTowardNoError = 383,  // G38.3
    ProbeAway          = 384,  // G38.4
    ProbeAwayNoError   = 385,  // G38.5
    None               = 800,  // G80
};

enum class Module : gcodenum_t {
    pen1  = 61,
    pen2  = 62,
    pen3  = 63,
    pen4  = 64,
    pen5  = 65,
    pen6  = 66,
    pen7  = 67,
    pen8  = 68,
    home  = 69,
    steps = 60,
};

// Modal Group G2: Plane select
enum class Plane : gcodenum_t {
    XY = 170,  // G17
    ZX = 180,  // G18
    YZ = 190,  // G19
};

// Modal Group G3: Distance mode
enum class Distance : gcodenum_t {
    Absolute    = 900,  // G90 Default
    Incremental = 910,  // G91
};

// Modal Group G4: Arc IJK distance mode
enum class ArcDistance : gcodenum_t {
    Incremental = 911,  // G91.1 Default
    Absolute    = 901,  // G90.1
};

// Modal Group M4: Program flow
enum class ProgramFlow : uint8_t {
    Running      = 0,   // Default
    Paused       = 3,   // M0
    OptionalStop = 1,   // M1 NOTE: Not supported, but valid and ignored.
    CompletedM2  = 2,   // M2
    CompletedM30 = 30,  // M30
};

// Modal Group G5: Feed rate mode
enum class FeedRate : gcodenum_t {
    UnitsPerMin = 940,  // G94 Default
    InverseTime = 930,  // G93
};

// Modal Group G6: Units mode
enum class Units : gcodenum_t {
    Mm     = 210,  // G21 Default
    Inches = 200,  // G20
};

// Modal Group G7: Cutter radius compensation mode
enum class CutterCompensation : gcodenum_t {
    Disable = 400,  // G40 Default
    Enable  = 410,
};

// Modal Group G13: Control mode
enum class ControlMode : gcodenum_t {
    ExactPath = 610,  // G61
};

// GCodeCoolant is used by the parser, where at most one of
// M7, M8, M9 may be present in a GCode block
enum class GCodeCoolant : gcodenum_t {
    None = 0,
    M7,
    M8,
    M9,
};

// CoolantState is used for the runtime state, where either of
// the Mist and Flood state bits can be set independently.
// Unlike GCode, overrides permit individual turn-off.
struct CoolantState {
    uint8_t Mist : 1;
    uint8_t Flood : 1;
};

// Modal Group M8: Coolant control
// Modal Group M9: Override control

// Modal Group M10: User I/O control
enum class IoControl : gcodenum_t {
    None                = 0,
    DigitalOnSync       = 1,  // M62
    DigitalOffSync      = 2,  // M63
    DigitalOnImmediate  = 3,  // M64
    DigitalOffImmediate = 4,  // M65
    SetAnalogSync       = 5,  // M67
    SetAnalogImmediate  = 6,  // M68
};

static const int MaxUserDigitalPin = 8;
static const int MaxUserAnalogPin  = 4;

// Modal Group G8: Tool length offset
enum class ToolLengthOffset : gcodenum_t {
    Cancel        = 490,  // G49 Default
    EnableDynamic = 431,  // G43.1
};

static const uint32_t MaxToolNumber = 99999999;
static const uint32_t MAX_PENS      = 10;  // Add this line

#define MAX_PENS 6  // Update maximum number of pens to 6

enum class ToolChange : uint8_t {
    Disable    = 0,
    Enable     = 1,
    InProgress = 2,
};

// Add this enum class definition before it's used:
enum class SetToolNumber : uint8_t {
    Disable = 0,
    Enable  = 1,
};

// Modal Group G12: Active work coordinate system
// N/A: Stores coordinate system value (54-59) to change to.

// Parameter word mapping.
enum class GCodeWord : uint8_t {
    E = 0,
    F = 1,
    I = 2,
    J = 3,
    K = 4,
    L = 5,
    N = 6,
    P = 7,
    Q = 8,
    R = 9,
    S = 10,
    T = 11,  // Tool selection for M6
    X = 12,
    Y = 13,
    Z = 14,
    A = 15,
    B = 16,
    C = 17,
    D = 18,  // For debugging
    U = 19,  // For module stepes
};

// GCode parser position updating flags
enum class GCUpdatePos : uint8_t {
    Target = 0,  // Must be zero
    System = 1,
    None   = 2,
};

// Various places in the code access saved coordinate system data
// by a small integer index according to the values below.
enum CoordIndex : uint16_t {
    Begin = 0,
    G54   = Begin,
    G55,
    G56,
    G57,
    G58,
    G59,
    // To support 9 work coordinate systems it would be necessary to define
    // the following 3 and modify GCode.cpp to support G59.1, G59.2, G59.3
    // G59_1,
    // G59_2,
    // G59_3,
    NWCSystems,
    G28 = NWCSystems,  // Home0
    G30,               // Home1
    G92,               // Temporary work offset
    TLO,               // Tool Length Offset, affected by G43.1 and G49
    End,
};

// Allow iteration over CoordIndex values
CoordIndex& operator++(CoordIndex& i);

// NOTE: When this struct is zeroed, the 0 values in the above types set the system defaults.
struct gc_modal_t {
    Motion           motion;        // {G0,G1,G2,G3,G38.2,G80}
    FeedRate         feed_rate;     // {G93,G94}
    Units            units;         // {G20,G21}
    Distance         distance;      // {G90,G91}
    Plane            plane_select;  // {G17,G18,G19}
    Module           module;        // controls the current module being used
    ToolLengthOffset tool_length;   // {G43.1,G49}
    CoordIndex       coord_select;  // {G54,G55,G56,G57,G58,G59}
    ProgramFlow      program_flow;  // {M0,M1,M2,M30}
    CoolantState     coolant;       // {M7,M8,M9}
    Override         override;      // {M56}
    ToolChange       tool_change;   // {M6}
    SetToolNumber    set_tool;      // For selecting which tool to use
    IoControl        io_control;    // For digital/analog I/O control
};

struct gc_values_t {
    uint8_t  e;                // M67
    float    f;                // Feed
    float    ijk[3];           // I,J,K Axis arc offsets - only 3 are possible
    uint8_t  l;                // G10 or canned cycles parameters
    int32_t  n;                // Line number
    float    p;                // G10 or dwell parameters
    float    q;                // M67
    float    r;                // Arc radius
    uint32_t t;                // Tool selection
    uint32_t u;                //module steps
    float    xyz[MAX_N_AXIS];  // X,Y,Z Translational axes
};

struct parser_state_t {
    gc_modal_t modal;

    float   feed_rate;    // Millimeters/min
    int32_t tool;         // Current pen number (0 = no pen)
    int32_t prev_tool;    // Previous pen number
    int32_t line_number;  // Last line number sent

    float position[MAX_N_AXIS];  // Where the interpreter considers the tool to be at this point in the code

    float coord_system[MAX_N_AXIS];  // Current work coordinate system (G54+). Stores offset from absolute machine
    // position in mm. Loaded from non-volatile storage when called.
    float coord_offset[MAX_N_AXIS];  // Retains the G92 coordinate offset (work coordinates) relative to
    // machine zero in mm. Non-persistent. Cleared upon reset and boot.
    float tool_length_offset;  // Tracks tool length offset value when enabled.
};

extern parser_state_t gc_state;

struct parser_block_t {
    NonModal     non_modal_command;
    gc_modal_t   modal;
    gc_values_t  values;
    GCodeCoolant coolant;
};

enum class AxisCommand : uint8_t {
    None             = 0,
    NonModal         = 1,
    MotionMode       = 2,
    ToolLengthOffset = 3,
    Module           = 4,
};

// Initialize the parser
void gc_init();

// Execute one block of rs275/ngc/g-code
Error gc_execute_line(char* line);

// Set g-code parser position. Input in steps.
void gc_sync_position();

void user_tool_change(uint32_t new_tool);
void user_m30();

void gc_ngc_changed(CoordIndex coord);
void gc_wco_changed();
void gc_ovr_changed();

extern gc_modal_t    modal_defaults;
extern volatile bool pen_change;

// Declare state system variables for laser offset tracking
extern bool  laser_offset_applied;
extern State last_machine_state;
extern bool  laser_offset_disabled;

void apply_laser_pointer_offset();
void remove_laser_pointer_offset();