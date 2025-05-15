/**
 * CNC Machine Control Pendant
 *
 * This firmware controls a pendant for a CNC machine using GRBL.
 * It provides LED feedback and button controls for common operations.
 *
 * Features:
 * - Visual LED feedback for machine states
 * - Jogging with short/long press
 * - Play/pause/hold operations
 * - Homing and alarm handling
 *
 * Hardware: Attiny1614 with NeoPixel LEDs
 */
#include <Arduino.h>
#include "GrblParserC.h"
#include <Adafruit_NeoPixel.h>
#include "LedConfig.hpp"
#include "main.h"

// Forward declarations
struct ButtonState;
void updateButtonState(ButtonState &btn, bool currentRead);
void handleButtons();
void updateLEDs();

//---------------------------------------------------------------
//                      Configuration
//---------------------------------------------------------------
// Jog settings
#define JOG_FEEDRATE 10000 // Feedrate for jogging in mm/min

// LED initialization - using constants from LedColors
Adafruit_NeoPixel pixels(LEDControl::LedColors::NUM_PIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

//---------------------------------------------------------------
//                      State Management
//---------------------------------------------------------------
// Machine state enumeration
enum MachineState
{
    RUNNING, // Machine is executing commands
    PAUSED,  // Machine is paused with feed hold
    IDLE,    // Machine is idle and ready
    JOGGING, // Machine is jogging
    HOMING,  // Machine is homing
    ALARM,   // Machine is in alarm state
    COMPLETE // Operation complete
};
MachineState machineState = IDLE;

// State tracking variables
bool isHoming = false;           // Whether machine is currently homing
bool justFinishedHoming = false; // Flag for homing completion animation
bool enableAlarmCheck = false;   // Whether to enable alarm checking
unsigned long startupTime = 0;   // Tracks startup time for initialization

// Buffer for current machine state text
char current_machine_state[20] = "Unknown";

//---------------------------------------------------------------
//                  Communication Functions
//---------------------------------------------------------------
/**
 * Get a character from serial if available
 */
int fnc_getchar()
{
    if (Serial.available())
    {
        return Serial.read();
    }
    return -1;
}

/**
 * Send a character to serial with a small delay
 */
void fnc_putchar(uint8_t ch)
{
    Serial.write(ch);
    delay(1); // Small delay to ensure reliable transmission
}

//---------------------------------------------------------------
//                   Button Handling
//---------------------------------------------------------------
// Structure to track button state with debouncing
struct ButtonState
{
    bool currentState = false;         // Current debounced state
    bool lastState = false;            // Previous state for edge detection
    bool isPressed = false;            // Button press detected (one-shot)
    bool isHeld = false;               // Button is being held
    uint32_t pressTime = 0;            // When button was pressed
    uint32_t lastDebounceTime = 0;     // Last time state changed for debounce
    bool longPressCommandSent = false; // Whether long press command was sent
};

// Button state instances
ButtonState upButton;
ButtonState rightButton;
ButtonState downButton;
ButtonState leftButton;
ButtonState playPauseButton;

//---------------------------------------------------------------
//                          Setup
//---------------------------------------------------------------
/**
 * Waits for the machine to report READY status before proceeding
 * Shows breathing red animation while waiting - no timeout
 */
void waitForMachineReady()
{
    // Set initial state
    machineState = ALARM;
    
    // Request status report
    fnc_realtime(StatusReport);
    
    // Keep running breathing animation while waiting
    unsigned long lastStatusRequest = 0;
    
    // Wait until machine enters HOMING state
    while (machineState != HOMING)
    {
        // Show breathing red animation during this waiting phase
        LEDControl::LedColors::breathingRedAnimation();
        
        // Request status periodically
        unsigned long currentTime = millis();
        if (currentTime - lastStatusRequest > 200)
        {
            fnc_realtime(StatusReport);
            lastStatusRequest = currentTime;
        }
        
        // Process incoming data
        fnc_poll();
    }
    // No extra delay here; proceed immediately
}

void setup()
{
    // Initialize hardware
    Serial.begin(115200);

    // Initialize buttons
    pinMode(BUTTON_UP, INPUT_PULLUP);
    pinMode(BUTTON_RIGHT, INPUT_PULLUP);
    pinMode(BUTTON_DOWN, INPUT_PULLUP);
    pinMode(BUTTON_LEFT, INPUT_PULLUP);
    pinMode(BUTTON_PLAYPAUSE, INPUT_PULLUP);

    // Set machine state to ALARM initially
    machineState = ALARM;
    
    // Initialize NeoPixel hardware 
    pixels.begin();
    pixels.setBrightness(LEDControl::LedColors::DEFAULT_BRIGHTNESS);
    pixels.clear();
    pixels.show();
    
    // Initialize LED colors
    LEDControl::LedColors::init(pixels);
    
    // Wait for GRBL to be ready and establish communication
    fnc_wait_ready();
    // Set a more frequent status report interval
    fnc_send_line("$Report/Interval=50", 100);
    delay(200);
    
    // Reset _machine_ready flag before waiting for it
    _machine_ready = false;
    
    // Wait for machine to report READY status while running breathing animation
    waitForMachineReady();
    
    // Now proceed with homing sequence
    LEDControl::LedColors::transitionToHoming();
    
    // Send homing command with delay to ensure it's sent properly
    delay(100); // Make sure any previous commands have been processed
    Serial.flush(); // Flush any pending serial data
    
    // Update state to HOMING - this will trigger the homing animation
    machineState = HOMING;
    isHoming = true;
}

//---------------------------------------------------------------
//                          Main Loop
//---------------------------------------------------------------
void loop()
{
    // Process serial data from GRBL
    fnc_poll();

    // Handle button presses
    handleButtons();

    // Update LEDs based on state
    updateLEDs();

    // Handle transition to idle after homing is complete
    if (justFinishedHoming && machineState == IDLE)
    {
        LEDControl::LedColors::transitionToGreen();
        justFinishedHoming = false;
    }
}

//---------------------------------------------------------------
//                  Button Processing Functions
//---------------------------------------------------------------
/**
 * Update button state with debouncing
 * @param btn Button state reference
 * @param currentRead Current raw button reading
 */
void updateButtonState(ButtonState &btn, bool currentRead)
{
    bool isPlayPauseButton = (&btn == &playPauseButton);
    
    // Check for state change
    if (currentRead != btn.lastState)
    {
        btn.lastDebounceTime = millis();
    }

    // Debounce
    if ((millis() - btn.lastDebounceTime) > 50)
    {
        // If state has changed after debounce window
        if (currentRead != btn.currentState)
        {
            btn.currentState = currentRead;

            if (btn.currentState == true)
            {
                // Button pressed
                btn.isPressed = true;
                btn.pressTime = millis();
                
                // Reset long press flag for ALL buttons when newly pressed
                // This ensures we can send multiple long press commands with the same button
                btn.longPressCommandSent = false;
            }
            else
            {
                // Button released
                if (isPlayPauseButton && btn.isHeld && !btn.longPressCommandSent) {
                    // If button was held but command wasn't sent, send it now
                    fnc_send_line("$H", 100);
                    btn.longPressCommandSent = true;
                }
                
                // Reset button state
                btn.isHeld = false;
                btn.isPressed = false;
                
                // Cancel jogging when direction buttons are released
                if (!isPlayPauseButton)
                {
                    fnc_realtime(JogCancel);
                }
            }
        }

        // Check for hold condition with different delays for different buttons
        if (btn.currentState == true && !btn.isHeld)
        {
            uint32_t holdDelay = isPlayPauseButton ? HOME_HOLD_DELAY : BUTTON_HOLD_DELAY;
            if ((millis() - btn.pressTime) > holdDelay)
            {
                btn.isHeld = true;
                
                // Immediately trigger the homing command when hold detected for play/pause button
                if (isPlayPauseButton && !btn.longPressCommandSent && 
                    (machineState == IDLE || machineState == JOGGING)) {
                    fnc_send_line("$H", 100);
                    btn.longPressCommandSent = true;
                }
            }
        }
    }

    btn.lastState = currentRead;
}

/**
 * Handle button presses based on machine state
 */
void handleButtons()
{
    // Read all buttons
    updateButtonState(upButton, !digitalRead(BUTTON_UP));
    updateButtonState(rightButton, !digitalRead(BUTTON_RIGHT));
    updateButtonState(downButton, !digitalRead(BUTTON_DOWN));
    updateButtonState(leftButton, !digitalRead(BUTTON_LEFT));
    updateButtonState(playPauseButton, !digitalRead(BUTTON_PLAYPAUSE));

    //---------------------------------------------------------------
    // Handle play/pause button based on machine state
    //---------------------------------------------------------------

    // ALARM state: short press => homing
    if (machineState == ALARM && playPauseButton.isPressed)
    {
        playPauseButton.isPressed = false;
        fnc_send_line("$H", 100);
        return;
    }

    // RUNNING/PAUSED: feed hold/cycle start
    if ((machineState == RUNNING || machineState == PAUSED) && playPauseButton.isPressed)
    {
        playPauseButton.isPressed = false;
        if (machineState == RUNNING)
        {
            fnc_putchar((uint8_t)FeedHold); // Pause
        }
        else
        {
            fnc_putchar((uint8_t)CycleStart); // Resume
        }
        return;
    }

    // IDLE/JOGGING: cycle start on short press only (long press handled in updateButtonState)
    if (playPauseButton.isPressed && !playPauseButton.isHeld)
    {
        // Only handle short press here - long press is handled in updateButtonState
        if (machineState == JOGGING)
        {
            fnc_putchar((uint8_t)FeedHold);
        }
        else if (machineState == IDLE)
        {
            fnc_putchar((uint8_t)CycleStart);
        }
        playPauseButton.isPressed = false;
        return;
    }

    //---------------------------------------------------------------
    //            Handle directional buttons for jogging
    //---------------------------------------------------------------
    if (machineState != PAUSED)
    {
        static uint32_t lastJogTime = 0;
        char jogCommand[32];

        // Add a small delay between jogs to prevent overwhelming the machine
        if ((millis() - lastJogTime) > 50)
        { // 50ms minimum between jogs
            
            // Handle directional buttons for jogging
            // Each one follows the same pattern:
            // 1. Check if button is pressed (short press) or held (long press)
            // 2. Send appropriate jog command
            // 3. Reset flags to prevent repeated commands
            // 4. Update lastJogTime to enforce minimum interval between commands

            // UP button
            if (upButton.isPressed && !upButton.isHeld && !upButton.longPressCommandSent)
            {
                // Short press - jog a short distance
                snprintf(jogCommand, sizeof(jogCommand), "$J=G91 G21 Y%d F%d",
                         SHORT_JOG_DISTANCE, JOG_FEEDRATE);
                fnc_send_line(jogCommand, 100);
                upButton.isPressed = false;
                lastJogTime = millis();
            }
            else if (upButton.isHeld && !upButton.longPressCommandSent)
            {
                // Long press - jog a longer distance
                snprintf(jogCommand, sizeof(jogCommand), "$J=G91 G21 Y%d F%d",
                         LONG_JOG_DISTANCE, JOG_FEEDRATE);
                fnc_send_line(jogCommand, 100);
                upButton.longPressCommandSent = true;
                lastJogTime = millis();
            }

            // RIGHT button
            if (rightButton.isPressed && !rightButton.isHeld && !rightButton.longPressCommandSent)
            {
                // Short press - jog a short distance
                snprintf(jogCommand, sizeof(jogCommand), "$J=G91 G21 X%d F%d",
                         SHORT_JOG_DISTANCE, JOG_FEEDRATE);
                fnc_send_line(jogCommand, 100);
                rightButton.isPressed = false;
                lastJogTime = millis();
            }
            else if (rightButton.isHeld && !rightButton.longPressCommandSent)
            {
                // Long press - jog a longer distance
                snprintf(jogCommand, sizeof(jogCommand), "$J=G91 G21 X%d F%d",
                         LONG_JOG_DISTANCE, JOG_FEEDRATE);
                fnc_send_line(jogCommand, 100);
                rightButton.longPressCommandSent = true;
                lastJogTime = millis();
            }

            // DOWN button
            if (downButton.isPressed && !downButton.isHeld && !downButton.longPressCommandSent)
            {
                // Short press - jog a short distance
                snprintf(jogCommand, sizeof(jogCommand), "$J=G91 G21 Y-%d F%d",
                         SHORT_JOG_DISTANCE, JOG_FEEDRATE);
                fnc_send_line(jogCommand, 100);
                downButton.isPressed = false;
                lastJogTime = millis();
            }
            else if (downButton.isHeld && !downButton.longPressCommandSent)
            {
                // Long press - jog a longer distance
                snprintf(jogCommand, sizeof(jogCommand), "$J=G91 G21 Y-%d F%d",
                         LONG_JOG_DISTANCE, JOG_FEEDRATE);
                fnc_send_line(jogCommand, 100);
                downButton.longPressCommandSent = true;
                lastJogTime = millis();
            }

            // LEFT button
            if (leftButton.isPressed && !leftButton.isHeld && !leftButton.longPressCommandSent)
            {
                // Short press - jog a short distance
                snprintf(jogCommand, sizeof(jogCommand), "$J=G91 G21 X-%d F%d",
                         SHORT_JOG_DISTANCE, JOG_FEEDRATE);
                fnc_send_line(jogCommand, 100);
                leftButton.isPressed = false;
                lastJogTime = millis();
            }
            else if (leftButton.isHeld && !leftButton.longPressCommandSent)
            {
                // Long press - jog a longer distance
                snprintf(jogCommand, sizeof(jogCommand), "$J=G91 G21 X-%d F%d",
                         LONG_JOG_DISTANCE, JOG_FEEDRATE);
                fnc_send_line(jogCommand, 100);
                leftButton.longPressCommandSent = true;
                lastJogTime = millis();
            }
        }
    }
}

//---------------------------------------------------------------
//                     LED Update Functions
//---------------------------------------------------------------
/**
 * Update LEDs based on machine state
 */
void updateLEDs()
{
    if (machineState == HOMING)
    {
        if (!isHoming) {
            // If not actually homing, don't run homing animation
            return;
        }
        LEDControl::LedColors::homingAnimation();
    }
    else if (machineState == IDLE)
    {
        if (!justFinishedHoming)
        {
            // Fix: Use explicit LED positions instead of array indexing
            pixels.setPixelColor(LEDControl::LedColors::LED_UP, LEDControl::LedColors::COLOR_GREEN);
            pixels.setPixelColor(LEDControl::LedColors::LED_RIGHT, LEDControl::LedColors::COLOR_GREEN);
            pixels.setPixelColor(LEDControl::LedColors::LED_DOWN, LEDControl::LedColors::COLOR_GREEN);
            pixels.setPixelColor(LEDControl::LedColors::LED_LEFT, LEDControl::LedColors::COLOR_GREEN);
            pixels.setPixelColor(LEDControl::LedColors::LED_PLAYPAUSE, LEDControl::LedColors::COLOR_OFF);
        }
    }
    else if (machineState == RUNNING)
    {
        // Explicitly turn off all arrow LEDs first
        pixels.setPixelColor(LEDControl::LedColors::LED_UP, LEDControl::LedColors::COLOR_OFF);
        pixels.setPixelColor(LEDControl::LedColors::LED_RIGHT, LEDControl::LedColors::COLOR_OFF);
        pixels.setPixelColor(LEDControl::LedColors::LED_DOWN, LEDControl::LedColors::COLOR_OFF);
        pixels.setPixelColor(LEDControl::LedColors::LED_LEFT, LEDControl::LedColors::COLOR_OFF);
        // Handle play/pause LED animation
        LEDControl::LedColors::runningAnimation(true);
    }
    else if (machineState == PAUSED)
    {
        // Explicitly turn off all arrow LEDs first
        pixels.setPixelColor(LEDControl::LedColors::LED_UP, LEDControl::LedColors::COLOR_OFF);
        pixels.setPixelColor(LEDControl::LedColors::LED_RIGHT, LEDControl::LedColors::COLOR_OFF);
        pixels.setPixelColor(LEDControl::LedColors::LED_DOWN, LEDControl::LedColors::COLOR_OFF);
        pixels.setPixelColor(LEDControl::LedColors::LED_LEFT, LEDControl::LedColors::COLOR_OFF);
        // Handle play/pause LED animation
        LEDControl::LedColors::pausedAnimation();
    }
    else if (machineState == ALARM)
    {
        LEDControl::LedColors::alarmAnimation();
    }

    pixels.show();
}

//---------------------------------------------------------------
//                  State Change Callback
//---------------------------------------------------------------
/**
 * Callback function for machine state changes
 * @param state New state as a string
 */
void show_state(const char *state)
{
    // Initialize startup timer on first state message
    if (startupTime == 0)
    {
        startupTime = millis();
    }

    // // Debug output to help diagnose state issues
    // Serial.print("State received: ");
    // Serial.println(state);

    // Store the current state
    strncpy(current_machine_state, state, sizeof(current_machine_state));

    if (strstr(state, "Run") == state)
    {
        // Check if we're transitioning from IDLE to RUNNING
        if (machineState == IDLE)
        {
            LEDControl::LedColors::transitionToOrange();
        }

        machineState = RUNNING;
        LEDControl::LedColors::updateMachineState(LEDControl::LedColors::RUNNING);
        isHoming = false;
    }
    else if (strstr(state, "Hold") == state)
    {
        machineState = PAUSED;
        LEDControl::LedColors::updateMachineState(LEDControl::LedColors::PAUSED);
        isHoming = false;
    }
    else if (strstr(state, "Idle") == state)
    {
        // Only process Idle state if we're not in startup mode
        if (isHoming)
        {
            justFinishedHoming = true;
            isHoming = false;
            machineState = IDLE;
            LEDControl::LedColors::updateMachineState(LEDControl::LedColors::IDLE);
            // Serial.println("Homing complete, transitioning to IDLE");
        }
        else if (machineState == HOMING)
        {
            justFinishedHoming = true;
            machineState = IDLE;
            LEDControl::LedColors::updateMachineState(LEDControl::LedColors::IDLE);
            // Serial.println("HOMING to IDLE transition");
        }
        else if (machineState == RUNNING)
        { 
            machineState = IDLE;
            LEDControl::LedColors::updateMachineState(LEDControl::LedColors::IDLE);
            LEDControl::LedColors::transitionToGreen();
            // Serial.println("RUNNING to IDLE transition");
        }
        else
        {
            machineState = IDLE;
            LEDControl::LedColors::updateMachineState(LEDControl::LedColors::IDLE);
        }
    }
    else if (strstr(state, "Alarm") == state)
    {
        machineState = ALARM;
        isHoming = false;
        // Serial.println("Machine in ALARM state");
    }
    else if (strstr(state, "Jog") == state)
    {
        machineState = JOGGING;
        isHoming = false;
    }
    else if (strstr(state, "Home") == state)
    {
        // For transitions from IDLE or RUNNING to HOMING
        if (machineState == IDLE || machineState == RUNNING)
        {
            LEDControl::LedColors::transitionToOrange();
        }

        machineState = HOMING;
        isHoming = true;
        justFinishedHoming = false;
        LEDControl::LedColors::isHomed = false; // Reset isHomed flag when starting homing
        // Serial.println("Machine in HOMING state");
    }

    updateLEDs();
}
