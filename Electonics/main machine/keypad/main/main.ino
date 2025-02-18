#include <Arduino.h>
#include "GrblParserC.h"
#include <Adafruit_NeoPixel.h>
#include "LedConfig.hpp"
#include "main.h"

// Forward declaration for the ButtonState structure.
struct ButtonState;

// ======================= Function Prototypes =======================
void updateButtonState(ButtonState &btn, bool currentRead);
void handleButtons();
void updateLEDs();
void handleSerialData();
void sendJogCommand(const char *axesCmd); // Helper to send jog command lines

// ======================= Configurable Settings =======================
#define JOG_FEEDRATE 10000 // Jog feedrate in mm/min
#define NUM_PIXELS 5       // Number of NeoPixels used in the setup

// ======================= Global Objects and Variables =======================
// Create a NeoPixel object using pin defined in main.h (NEOPIXEL_PIN)
Adafruit_NeoPixel pixels(NUM_PIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
uint8_t currentBrightness = DEFAULT_BRIGHTNESS; // Set brightness to default

// UART functions to read and write characters for Grbl communication
int fnc_getchar()
{
    return Serial.available() ? Serial.read() : -1;
}
void fnc_putchar(uint8_t ch)
{
    Serial.write(ch);
    delay(1);
}

// Global buffers for incoming serial data and current machine state string.
String inputBuffer = "";
bool messageComplete = false;
char current_machine_state[20] = "Unknown";

// ======================= Timing and Jog Definitions =======================
#define BUTTON_HOLD_DELAY 500  // Delay before a button is considered 'held'
#define HOME_HOLD_DELAY 1000   // Longer delay for the play/pause button (for homing)
#define SHORT_JOG_DISTANCE 1   // Short jog command distance (mm)
#define LONG_JOG_DISTANCE 1000 // Continuous jog command distance (mm)

// ======================= Button State Structure =======================
// This structure holds the debounced button state and timing information.
struct ButtonState
{
    bool currentState = false;
    bool lastState = false;
    bool isPressed = false;            // Indicates a new press
    bool isHeld = false;               // True if press duration exceeds defined delay
    uint32_t pressTime = 0;            // Time when the button was pressed
    uint32_t lastDebounceTime = 0;     // Last time the button input was changed
    bool longPressCommandSent = false; // To prevent repeated command sending during long presses
};

// Button instances corresponding to different control buttons.
ButtonState upButton, rightButton, downButton, leftButton, playPauseButton;

// Global flags and the machine state.
// These variables are declared extern in main.h.
bool alarm14Active = false;
bool inStartupPhase = true;
bool homingComplete = false;
bool isHoming = false;
MachineState machineState = IDLE;

// ======================= State Parsing Function =======================
// Parse a state string from the controller and return a corresponding enumerated state.
ParsedState parseStateString(const char *s)
{
    if (strstr(s, "Run") == s)
        return STATE_RUN;
    if (strstr(s, "Hold") == s)
        return STATE_HOLD;
    if (strstr(s, "Idle") == s)
        return STATE_IDLE;
    if (strstr(s, "Alarm") == s)
        return STATE_ALARM;
    if (strstr(s, "Jog") == s)
        return STATE_JOG;
    if (strstr(s, "Home") == s)
        return STATE_HOME;
    return STATE_UNKNOWN;
}

// ======================= Setup Function =======================
// Initialize serial communication, buttons, NeoPixels, and request initial machine status.
void setup()
{
    Serial.begin(115200);
    delay(1000);

    // Configure button pins as inputs with internal pull-ups.
    pinMode(BUTTON_UP, INPUT_PULLUP);
    pinMode(BUTTON_RIGHT, INPUT_PULLUP);
    pinMode(BUTTON_DOWN, INPUT_PULLUP);
    pinMode(BUTTON_LEFT, INPUT_PULLUP);
    pinMode(BUTTON_PLAYPAUSE, INPUT_PULLUP);

    // Initialize and test the NeoPixel array.
    pixels.begin();
    pixels.clear();
    pixels.setBrightness(DEFAULT_BRIGHTNESS);
    for (int i = 0; i < NUM_PIXELS; i++)
    {
        pixels.setPixelColor(i, pixels.Color(255, 255, 255));
    }
    pixels.show();
    delay(500);
    pixels.clear();
    pixels.show();

    // Initialize LED control module.
    LEDControl::LedColors::init(pixels);
    machineState = IDLE;
    // Set initial LED state (e.g., arrow LEDs green).
    for (int i = 0; i < NUM_PIXELS; i++)
    {
        pixels.setPixelColor(i, LEDControl::LedColors::COLOR_GREEN);
    }
    pixels.show();

    // Wait for the controller to signal ready status.
    fnc_wait_ready();
    fnc_send_line("$Report/Interval=50", 100);
    delay(100);
    fnc_send_line("$H", 100);
    delay(1000);
}

// ======================= Serial Data Handler =======================
// Collect incoming serial data and set a flag when a full message is received.
void handleSerialData()
{
    while (Serial.available())
    {
        char inChar = (char)Serial.read();
        if (inChar != '\n')
            inputBuffer += inChar;
        else
            messageComplete = true;
    }
    if (messageComplete)
    {
        // Message processing could occur here.
        inputBuffer = "";
        messageComplete = false;
    }
}

// ======================= Main Loop =======================
// The loop polls for serial data, processes button actions and updates LED animations.
void loop()
{
    fnc_poll();
    handleButtons();
    updateLEDs();
    fnc_poll();
}

// ======================= Button State Update Function =======================
// Debounces the button and updates its current and hold states.
void updateButtonState(ButtonState &btn, bool currentRead)
{
    if (currentRead != btn.lastState)
        btn.lastDebounceTime = millis();
    if ((millis() - btn.lastDebounceTime) > 50)
    {
        if (currentRead != btn.currentState)
        {
            btn.currentState = currentRead;
            if (btn.currentState)
            {
                btn.isPressed = true;
                btn.pressTime = millis();
            }
            else
            {
                btn.isHeld = false;
                btn.isPressed = false;
                btn.longPressCommandSent = false;
                // When a directional button is released, send a jog cancel command.
                if (&btn != &playPauseButton)
                    fnc_putchar((uint8_t)JogCancel);
            }
        }
        // Once the button is held longer than the defined delay, mark it as held.
        if (btn.currentState && !btn.isHeld)
        {
            uint32_t holdDelay = (&btn == &playPauseButton) ? HOME_HOLD_DELAY : BUTTON_HOLD_DELAY;
            if ((millis() - btn.pressTime) > holdDelay)
                btn.isHeld = true;
        }
    }
    btn.lastState = currentRead;
}

// ======================= Pixel Update Helper =======================
// Sets all NeoPixels to a given color for consistent LED updates.
static void setAllPixels(uint32_t color)
{
    for (uint8_t i = 0; i < NUM_PIXELS; i++)
    {
        pixels.setPixelColor(i, color);
    }
}

// ======================= Button Handling Function =======================
// Processes each button's state to send appropriate commands.
// Uses a switch-case for the play/pause (homing) functionality and processes jog commands.
void handleButtons()
{
    updateButtonState(upButton, !digitalRead(BUTTON_UP));
    updateButtonState(rightButton, !digitalRead(BUTTON_RIGHT));
    updateButtonState(downButton, !digitalRead(BUTTON_DOWN));
    updateButtonState(leftButton, !digitalRead(BUTTON_LEFT));
    updateButtonState(playPauseButton, !digitalRead(BUTTON_PLAYPAUSE));

    // If an alarm is active, send homing command when play/pause is pressed.
    if (alarm14Active)
    {
        if (playPauseButton.isPressed)
        {
            playPauseButton.isPressed = false;
            fnc_send_line("$H", 100);
        }
        return;
    }

    // If play/pause button is pressed:
    // - on a long press or if in ALARM state, send a homing command.
    // - on short press, decide between pausing or starting based on machine state.
    if (playPauseButton.isPressed)
    {
        if ((playPauseButton.isHeld && !playPauseButton.longPressCommandSent) || (machineState == ALARM))
        {
            fnc_send_line("$H", 100);
            playPauseButton.longPressCommandSent = true;
            return;
        }
        if (!playPauseButton.isHeld)
        {
            switch (machineState)
            {
            case RUNNING:
            case JOGGING:
                fnc_putchar((uint8_t)FeedHold); // Pause command.
                break;
            case PAUSED:
            case IDLE:
                fnc_putchar((uint8_t)CycleStart); // Start command.
                break;
            default:
                break;
            }
            playPauseButton.isPressed = false;
            return;
        }
    }

    // If machine is not paused, process directional jog commands.
    if (machineState != PAUSED)
    {
        static uint32_t lastJogTime = 0;
        char jogCommand[32];
        if ((millis() - lastJogTime) > 50)
        { // Enforce minimum delay between jog commands.
            // Up button jog handling.
            if (upButton.isPressed && !upButton.isHeld && !upButton.longPressCommandSent)
            {
                snprintf(jogCommand, sizeof(jogCommand), "$J=G91 G21 Y%d F%d", SHORT_JOG_DISTANCE, JOG_FEEDRATE);
                sendJogCommand(jogCommand);
                upButton.isPressed = false;
                lastJogTime = millis();
            }
            else if (upButton.isHeld && !upButton.longPressCommandSent)
            {
                snprintf(jogCommand, sizeof(jogCommand), "$J=G91 G21 Y%d F%d", LONG_JOG_DISTANCE, JOG_FEEDRATE);
                sendJogCommand(jogCommand);
                upButton.longPressCommandSent = true;
                lastJogTime = millis();
            }
            // Right button jog handling.
            if (rightButton.isPressed && !rightButton.isHeld && !rightButton.longPressCommandSent)
            {
                snprintf(jogCommand, sizeof(jogCommand), "$J=G91 G21 X%d F%d", SHORT_JOG_DISTANCE, JOG_FEEDRATE);
                sendJogCommand(jogCommand);
                rightButton.isPressed = false;
                lastJogTime = millis();
            }
            else if (rightButton.isHeld && !rightButton.longPressCommandSent)
            {
                snprintf(jogCommand, sizeof(jogCommand), "$J=G91 G21 X%d F%d", LONG_JOG_DISTANCE, JOG_FEEDRATE);
                sendJogCommand(jogCommand);
                rightButton.longPressCommandSent = true;
                lastJogTime = millis();
            }
            // Down button jog handling.
            if (downButton.isPressed && !downButton.isHeld && !downButton.longPressCommandSent)
            {
                snprintf(jogCommand, sizeof(jogCommand), "$J=G91 G21 Y-%d F%d", SHORT_JOG_DISTANCE, JOG_FEEDRATE);
                sendJogCommand(jogCommand);
                downButton.isPressed = false;
                lastJogTime = millis();
            }
            else if (downButton.isHeld && !downButton.longPressCommandSent)
            {
                snprintf(jogCommand, sizeof(jogCommand), "$J=G91 G21 Y-%d F%d", LONG_JOG_DISTANCE, JOG_FEEDRATE);
                sendJogCommand(jogCommand);
                downButton.longPressCommandSent = true;
                lastJogTime = millis();
            }
            // Left button jog handling.
            if (leftButton.isPressed && !leftButton.isHeld && !leftButton.longPressCommandSent)
            {
                snprintf(jogCommand, sizeof(jogCommand), "$J=G91 G21 X-%d F%d", SHORT_JOG_DISTANCE, JOG_FEEDRATE);
                sendJogCommand(jogCommand);
                leftButton.isPressed = false;
                lastJogTime = millis();
            }
            else if (leftButton.isHeld && !leftButton.longPressCommandSent)
            {
                snprintf(jogCommand, sizeof(jogCommand), "$J=G91 G21 X-%d F%d", LONG_JOG_DISTANCE, JOG_FEEDRATE);
                sendJogCommand(jogCommand);
                leftButton.longPressCommandSent = true;
                lastJogTime = millis();
            }
        }
    }
}

// ======================= LED Update Function =======================
// Update LED colors and animations based on the current machine state.
void updateLEDs()
{
    if (inStartupPhase && !isHoming)
    {
        LEDControl::LedColors::machineInitAnimation();
        return;
    }

    switch (machineState)
    {
    case IDLE:
        setAllPixels(LEDControl::LedColors::COLOR_GREEN);
        pixels.setPixelColor(LED_PLAYPAUSE, LEDControl::LedColors::COLOR_OFF);
        pixels.show();
        break;
    case RUNNING:
        setAllPixels(LEDControl::LedColors::COLOR_OFF);
        LEDControl::LedColors::runningAnimation(true);
        pixels.show();
        break;
    case PAUSED:
        setAllPixels(LEDControl::LedColors::COLOR_OFF);
        LEDControl::LedColors::pausedAnimation();
        pixels.show();
        break;
    case ALARM:
        LEDControl::LedColors::alarmAnimation();
        pixels.show();
        break;
    default:
        pixels.show();
        break;
    }
}

// ======================= State Change Handler =======================
// Parse a new state string, update machine state and related flags, then refresh LED display.
void show_state(const char *state)
{
    strncpy(current_machine_state, state, sizeof(current_machine_state));
    ParsedState ps = parseStateString(state);
    switch (ps)
    {
    case STATE_RUN:
        machineState = RUNNING;
        alarm14Active = false;
        isHoming = false;
        break;
    case STATE_HOLD:
        machineState = PAUSED;
        isHoming = false;
        break;
    case STATE_IDLE:
        machineState = IDLE;
        alarm14Active = false;
        isHoming = false;
        break;
    case STATE_ALARM:
        machineState = ALARM;
        alarm14Active = false;
        isHoming = false;
        break;
    case STATE_JOG:
        machineState = JOGGING;
        isHoming = false;
        break;
    case STATE_HOME:
        isHoming = true;
        break;
    default:
        break;
    }
    updateLEDs();
}

// ======================= Jog Command Sender =======================
// Sends out a jog command string using the Grbl communication functions.
void sendJogCommand(const char *axesCmd)
{
    fnc_send_line(axesCmd, 100);
}