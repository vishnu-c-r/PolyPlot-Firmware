#ifndef MAIN_H
#define MAIN_H

// Pin definitions
static const uint8_t BUTTON_UP = 9;
static const uint8_t BUTTON_RIGHT = 8;
static const uint8_t BUTTON_DOWN = 1;
static const uint8_t BUTTON_LEFT = 0;
static const uint8_t BUTTON_PLAYPAUSE = 3;
static const uint8_t NEOPIXEL_PIN = 10;

// Button timing
static const uint16_t BUTTON_HOLD_DELAY = 500;
static const uint16_t HOME_HOLD_DELAY = 1000;

// Movement settings
static const uint16_t JOG_FEEDRATE = 10000;
static const uint16_t SHORT_JOG_DISTANCE = 1;
static const uint16_t LONG_JOG_DISTANCE = 1000;

// Forward declare the ButtonState struct if needed globally:
struct ButtonState;

// Move the machine states here:
enum MachineState
{
    RUNNING,
    PAUSED,
    IDLE,
    JOGGING,
    ALARM,
    COMPLETE
};
extern MachineState machineState;

// Move the parsed states here:
enum ParsedState
{
    STATE_RUN,
    STATE_HOLD,
    STATE_IDLE,
    STATE_ALARM,
    STATE_JOG,
    STATE_HOME,
    STATE_UNKNOWN
};

// Expose function prototype for parsing states:
ParsedState parseStateString(const char *s);

// Move these global flags if needed globally:
extern bool alarm14Active;
extern bool inStartupPhase;
extern bool homingComplete;
extern bool isHoming;

#endif
