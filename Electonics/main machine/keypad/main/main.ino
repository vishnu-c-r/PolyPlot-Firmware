#include <Arduino.h>
#include "GrblParserC.h"
#include <Adafruit_NeoPixel.h>
#include "LedConfig.hpp"
#include "main.h" // Include main.h

// Forward declare the ButtonState struct
struct ButtonState;

// Function prototypes
void updateButtonState(ButtonState& btn, bool currentRead);
void handleButtons();
void updateLEDs();

//controller used: Attiny1614
//pinout: https://www.microchip.com/wwwproducts/en/ATtiny1614

// Configurable settings
#define JOG_FEEDRATE 10000      // Feedrate for jogging in mm/min

// Use class constants instead of #defines
Adafruit_NeoPixel pixels(LEDControl::LedColors::NUM_PIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// Machine state enumeration
enum MachineState { RUNNING, PAUSED, IDLE, JOGGING, HOMING, ALARM, COMPLETE };  // Added HOMING
MachineState machineState = IDLE;  // Changed from RUNNING to IDLE

// UART functions required by GrblParserC
int fnc_getchar() {
    if (Serial.available()) {
        return Serial.read();
    }
    return -1;
}

void fnc_putchar(uint8_t ch) {
    Serial.write(ch);
    delay(1);  // Add small delay to ensure reliable transmission
}

int milliseconds() {
    return millis();
}

// Helper macro for converting numbers to strings
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

// Add these global variables near the top
String inputBuffer = "";
bool messageComplete = false;

// Add these declarations near the top of the file after the #include statements
void fnc_send_play_command();
void fnc_send_pause_command();
void fnc_send_cancel_command();
void fnc_send_home_command();
char current_machine_state[20] = "Unknown";  // Define the variable here

// Define the ButtonState struct
struct ButtonState {
    bool currentState = false;
    bool lastState = false;
    bool isPressed = false;
    bool isHeld = false;
    uint32_t pressTime = 0;
    uint32_t lastDebounceTime = 0;
    bool longPressCommandSent = false; // Add this
};

// Declare button states as global variables
ButtonState upButton;
ButtonState rightButton;
ButtonState downButton;
ButtonState leftButton;
ButtonState playPauseButton;

// Add this global variable near the top
bool alarm14Active = false;

// OPTIMIZATION: Add a dedicated flag for homing (instead of checking current_machine_state text)
bool isHoming = false;

// Add near other state variables
bool justFinishedHoming = false;  // Add this flag

// Add a new variable at the top with other globals
bool enableAlarmCheck = false;
unsigned long startupTime = 0;

void setup() {
    // Initialize hardware
    Serial.begin(115200);
    delay(1000);

    // Initialize buttons
    pinMode(BUTTON_UP, INPUT_PULLUP);
    pinMode(BUTTON_RIGHT, INPUT_PULLUP);
    pinMode(BUTTON_DOWN, INPUT_PULLUP);
    pinMode(BUTTON_LEFT, INPUT_PULLUP);
    pinMode(BUTTON_PLAYPAUSE, INPUT_PULLUP);

    // Initialize LEDs and start animation
    pixels.begin();
    pixels.clear();
    pixels.setBrightness(LEDControl::LedColors::DEFAULT_BRIGHTNESS);
    LEDControl::LedColors::init(pixels);
    
    // Run startup animation for 3 seconds before checking machine state
    unsigned long startTime = millis();
    while (millis() - startTime < 3000) {
        LEDControl::LedColors::startupAnimation();
    }
    machineState = HOMING;  // Set initial state to HOMING
    
    // Now begin machine communication
    fnc_wait_ready();
    fnc_send_line("$Report/Interval=50", 100);
    delay(100);
    
    // Send homing command
    fnc_send_line("$H", 100);
    delay(1000);
    
    // Initialize state
    machineState = HOMING;
    isHoming = true;
    alarm14Active = false;  // Start with alarm check disabled
}

// Add this function after setup()
void handleSerialData() {
    while (Serial.available()) {
        char inChar = (char)Serial.read();
        if (inChar == '\n' || inputBuffer.length() >= REPORT_BUFFER_LEN - 1) {
            messageComplete = true;
        } else {
            inputBuffer += inChar;
        }
    }
    if (messageComplete) {
        // Process the message here
        // Serial.print(F("Received: "));
        // Serial.println(inputBuffer);
        inputBuffer = "";
        messageComplete = false;
    }
}

// Update loop() to ensure LED state is always updated
void loop() {
    // Poll more frequently for state changes
    fnc_poll();  // Process any available serial data
    handleButtons();
    updateLEDs();
    fnc_poll();  // Check again after processing
}

// Move these function implementations to after loop() but before the other functions
void fnc_send_play_command() {
    fnc_realtime(CycleStart);
}
void fnc_send_pause_command() {
    fnc_realtime(FeedHold);
}

// Add this with the other command functions
void fnc_send_home_command() {
    fnc_send_line("$H", 100);  // Send the homing command
}

// Add this helper function before handleButtons()
void updateButtonState(ButtonState& btn, bool currentRead) {
    if (currentRead != btn.lastState) {
        btn.lastDebounceTime = millis();
    }
    
    if ((millis() - btn.lastDebounceTime) > 50) {
        if (currentRead != btn.currentState) {
            btn.currentState = currentRead;
            
            if (btn.currentState == true) {
                btn.isPressed = true;
                btn.pressTime = millis();
            } else {
                btn.isHeld = false;
                btn.isPressed = false;
                btn.longPressCommandSent = false;
                // Only send JogCancel for direction buttons
                if (&btn != &playPauseButton) {
                    fnc_realtime(JogCancel);
                }
            }
        }
        
        // Check for hold condition with different delays
        if (btn.currentState == true && !btn.isHeld) {
            uint32_t holdDelay = (&btn == &playPauseButton) ? HOME_HOLD_DELAY : BUTTON_HOLD_DELAY;
            if ((millis() - btn.pressTime) > holdDelay) {
                btn.isHeld = true;
            }
        }
    }
    
    btn.lastState = currentRead;
}

// Replace the existing handleButtons() function with this version
void handleButtons() {
    // Update all button states
    updateButtonState(upButton, !digitalRead(BUTTON_UP));
    updateButtonState(rightButton, !digitalRead(BUTTON_RIGHT));
    updateButtonState(downButton, !digitalRead(BUTTON_DOWN));
    updateButtonState(leftButton, !digitalRead(BUTTON_LEFT));
    updateButtonState(playPauseButton, !digitalRead(BUTTON_PLAYPAUSE));

    // If Alarm 14 is active, only allow Home button to initiate homing
    if (alarm14Active) {
        if (playPauseButton.isPressed) {
            playPauseButton.isPressed = false;
            fnc_send_home_command();
        }
        return;
    }

    // Handle Play/Pause button first
    if (playPauseButton.isPressed) {
        if (playPauseButton.isHeld && !playPauseButton.longPressCommandSent) {
            // Handle long press for home - Fixed homing command
            fnc_send_line("$H", 100);  // Send homing command directly
            playPauseButton.longPressCommandSent = true;
        } else if (!playPauseButton.isHeld) {
            // Handle short press for play/pause
            if (machineState == RUNNING || machineState == JOGGING) {
                fnc_putchar((uint8_t)FeedHold);
            } else if (machineState == PAUSED || machineState == IDLE) {
                fnc_putchar((uint8_t)CycleStart);
            }
            playPauseButton.isPressed = false;
        }
        return;
    }

    // Process jog commands when not paused
    if (machineState != PAUSED) {
        static uint32_t lastJogTime = 0;
        char jogCommand[32];
        
        // Add a small delay between jogs to prevent overwhelming the machine
        if ((millis() - lastJogTime) > 50) {  // 50ms minimum between jogs
            if (upButton.isPressed && !upButton.isHeld && !upButton.longPressCommandSent) {
                snprintf(jogCommand, sizeof(jogCommand), "$J=G91 G21 Y%d F%d", SHORT_JOG_DISTANCE, JOG_FEEDRATE);
                fnc_send_line(jogCommand, 100);
                upButton.isPressed = false;
                lastJogTime = millis();
            } else if (upButton.isHeld && !upButton.longPressCommandSent) {
                snprintf(jogCommand, sizeof(jogCommand), "$J=G91 G21 Y%d F%d", LONG_JOG_DISTANCE, JOG_FEEDRATE);
                fnc_send_line(jogCommand, 100);
                upButton.longPressCommandSent = true;
                lastJogTime = millis();
            }
            
            if (rightButton.isPressed && !rightButton.isHeld && !rightButton.longPressCommandSent) {
                snprintf(jogCommand, sizeof(jogCommand), "$J=G91 G21 X%d F%d", SHORT_JOG_DISTANCE, JOG_FEEDRATE);
                fnc_send_line(jogCommand, 100);
                rightButton.isPressed = false;
                lastJogTime = millis();
            } else if (rightButton.isHeld && !rightButton.longPressCommandSent) {
                snprintf(jogCommand, sizeof(jogCommand), "$J=G91 G21 X%d F%d", LONG_JOG_DISTANCE, JOG_FEEDRATE);
                fnc_send_line(jogCommand, 100);
                rightButton.longPressCommandSent = true;
                lastJogTime = millis();
            }
            
            if (downButton.isPressed && !downButton.isHeld && !downButton.longPressCommandSent) {
                snprintf(jogCommand, sizeof(jogCommand), "$J=G91 G21 Y-%d F%d", SHORT_JOG_DISTANCE, JOG_FEEDRATE);
                fnc_send_line(jogCommand, 100);
                downButton.isPressed = false;
                lastJogTime = millis();
            } else if (downButton.isHeld && !downButton.longPressCommandSent) {
                snprintf(jogCommand, sizeof(jogCommand), "$J=G91 G21 Y-%d F%d", LONG_JOG_DISTANCE, JOG_FEEDRATE);
                fnc_send_line(jogCommand, 100);
                downButton.longPressCommandSent = true;
                lastJogTime = millis();
            }
            
            if (leftButton.isPressed && !leftButton.isHeld && !leftButton.longPressCommandSent) {
                snprintf(jogCommand, sizeof(jogCommand), "$J=G91 G21 X-%d F%d", SHORT_JOG_DISTANCE, JOG_FEEDRATE);
                fnc_send_line(jogCommand, 100);
                leftButton.isPressed = false;
                lastJogTime = millis();
            } else if (leftButton.isHeld && !leftButton.longPressCommandSent) {
                snprintf(jogCommand, sizeof(jogCommand), "$J=G91 G21 X-%d F%d", LONG_JOG_DISTANCE, JOG_FEEDRATE);
                fnc_send_line(jogCommand, 100);
                leftButton.longPressCommandSent = true;
                lastJogTime = millis();
            }
        }
    }
}


void updateLEDs() {
    // If in startup mode, only show startup animation
    if (LEDControl::LedColors::inStartupMode) {
        LEDControl::LedColors::startupAnimation();
        return;
    }

    static MachineState prevState = machineState;
    static bool transitionActive = false;
    static unsigned long transitionStart = 0;

    // Skip transitions for: IDLE<->JOGGING and RUNNING<->PAUSED
    bool skipTransition = (machineState == IDLE && prevState == JOGGING) || 
                         (machineState == JOGGING && prevState == IDLE) ||
                         ((machineState == RUNNING && prevState == PAUSED) || 
                          (machineState == PAUSED && prevState == RUNNING));

    // Start transition when state changes (skip for ALARM and specified state pairs)
    if ((machineState != prevState) && (machineState != ALARM) && !skipTransition) {
        transitionActive = true;
        transitionStart = millis();
        prevState = machineState;  // Move this here to ensure state is updated
    } else if (skipTransition) {
        prevState = machineState;  // Update state without transition
    }

    auto interp = [&](uint32_t from, uint32_t to, uint8_t progress) -> uint32_t {
        auto getR = [](uint32_t col) -> uint8_t { return (col >> 16) & 0xFF; };
        auto getG = [](uint32_t col) -> uint8_t { return (col >> 8) & 0xFF; };
        auto getB = [](uint32_t col) -> uint8_t { return col & 0xFF; };
        uint8_t r = getR(from) + ((getR(to) - getR(from)) * progress) / 255;
        uint8_t g = getG(from) + ((getG(to) - getG(from)) * progress) / 255;
        uint8_t b = getB(from) + ((getB(to) - getB(from)) * progress) / 255;
        return pixels.Color(r, g, b);
    };

    if (transitionActive) {
        uint32_t targetColor;
        switch (machineState) {
            case IDLE:    targetColor = LEDControl::LedColors::COLOR_GREEN; break;
            case RUNNING: targetColor = LEDControl::LedColors::COLOR_ORANGE; break;
            case PAUSED:  targetColor = LEDControl::LedColors::COLOR_ORANGE; break;
            case JOGGING: targetColor = LEDControl::LedColors::COLOR_GREEN; break;
            case HOMING:
                // During startup phase use orange and then switch to cyan after
                if (LEDControl::LedColors::inStartupMode) {
                    targetColor = LEDControl::LedColors::COLOR_ORANGE;
                } else {
                    targetColor = pixels.Color(0, 255, 255); // Cyan for homing
                }
                break;
            default:      targetColor = LEDControl::LedColors::COLOR_OFF; break;
        }
        uint32_t fromColor = pixels.getPixelColor(0);
        uint32_t newColor;
        uint32_t elapsed = millis() - transitionStart;
        if (elapsed >= LEDControl::LedColors::TRANSITION_DURATION) {
            transitionActive = false;
            newColor = targetColor;
        } else {
            uint8_t progress = (elapsed * 255UL) / LEDControl::LedColors::TRANSITION_DURATION;
            newColor = interp(fromColor, targetColor, progress);
        }
        for (int i = 0; i < LEDControl::LedColors::NUM_PIXELS; i++) {
            pixels.setPixelColor(i, newColor);
        }
        pixels.show();
        return;  // Skip state animations until transition finishes
    }

    // State-specific animations when not transitioning:
    if (machineState == HOMING) {
        if (LEDControl::LedColors::inStartupMode) {
            LEDControl::LedColors::startupAnimation();
        } else {
            LEDControl::LedColors::homingAnimation();
        }
    } else if (machineState == IDLE) {
        if (justFinishedHoming) {
            LEDControl::LedColors::readyBlinkAnimation();
            if (LEDControl::LedColors::blinkCount >= 3) {
                justFinishedHoming = false;
                LEDControl::LedColors::blinkCount = 0;
            }
        } else {
            // Fix: Use explicit LED positions instead of array indexing
            pixels.setPixelColor(LEDControl::LedColors::LED_UP, LEDControl::LedColors::COLOR_GREEN);
            pixels.setPixelColor(LEDControl::LedColors::LED_RIGHT, LEDControl::LedColors::COLOR_GREEN);
            pixels.setPixelColor(LEDControl::LedColors::LED_DOWN, LEDControl::LedColors::COLOR_GREEN);
            pixels.setPixelColor(LEDControl::LedColors::LED_LEFT, LEDControl::LedColors::COLOR_GREEN);
            pixels.setPixelColor(LEDControl::LedColors::LED_PLAYPAUSE, LEDControl::LedColors::COLOR_OFF);
        }
    } 
    else if (machineState == RUNNING) {
        // Explicitly turn off all arrow LEDs first
        pixels.setPixelColor(LEDControl::LedColors::LED_UP, LEDControl::LedColors::COLOR_OFF);
        pixels.setPixelColor(LEDControl::LedColors::LED_RIGHT, LEDControl::LedColors::COLOR_OFF);
        pixels.setPixelColor(LEDControl::LedColors::LED_DOWN, LEDControl::LedColors::COLOR_OFF);
        pixels.setPixelColor(LEDControl::LedColors::LED_LEFT, LEDControl::LedColors::COLOR_OFF);
        
        // Handle play/pause LED animation
        LEDControl::LedColors::runningAnimation(true);
    } 
    else if (machineState == PAUSED) {
        // Explicitly turn off all arrow LEDs first
        pixels.setPixelColor(LEDControl::LedColors::LED_UP, LEDControl::LedColors::COLOR_OFF);
        pixels.setPixelColor(LEDControl::LedColors::LED_RIGHT, LEDControl::LedColors::COLOR_OFF);
        pixels.setPixelColor(LEDControl::LedColors::LED_DOWN, LEDControl::LedColors::COLOR_OFF);
        pixels.setPixelColor(LEDControl::LedColors::LED_LEFT, LEDControl::LedColors::COLOR_OFF);
        
        // Handle play/pause LED animation
        LEDControl::LedColors::pausedAnimation();
    } else if (machineState == RUNNING ) {
        // Arrow LEDs OFF; PLAY/PAUSE LED flickers orange
        int arrowLEDs[4] = {
            LEDControl::LedColors::LED_UP,
            LEDControl::LedColors::LED_RIGHT,
            LEDControl::LedColors::LED_DOWN,
            LEDControl::LedColors::LED_LEFT
        };
        for (int i = 0; i < 4; i++) {
            pixels.setPixelColor(arrowLEDs[i], LEDControl::LedColors::COLOR_OFF);
        }
        // Add running animation for play/pause LED when in RUNNING state
        if (machineState == RUNNING) {
            LEDControl::LedColors::runningAnimation(true); // true enables the flicker effect
        }
    } else if (machineState == PAUSED) {
        // PAUSED: arrow LEDs off; PLAY/PAUSE breathes (handled by pausedAnimation)
        for (int i = 0; i < 4; i++) {
            pixels.setPixelColor(i, LEDControl::LedColors::COLOR_OFF);
        }
        LEDControl::LedColors::pausedAnimation();
    } else if (machineState == HOMING) {
        LEDControl::LedColors::homingAnimation();
    } else if (machineState == ALARM) {
        LEDControl::LedColors::alarmAnimation();
    }

    pixels.show();
}

// Callback function for machine state changes
void show_state(const char* state) {
    // Initialize startup timer on first state message
    if (startupTime == 0) {
        startupTime = millis();
        LEDControl::LedColors::inStartupMode = true;  // Ensure startup mode is active
    }
    
    // Ignore state changes during initial startup period
    if (millis() - startupTime < 3000) {
        return;  // Skip state processing during startup animation
    }

    strncpy(current_machine_state, state, sizeof(current_machine_state));
    
    if (strstr(state, "Run") == state) {
        LEDControl::LedColors::inStartupMode = false;
        machineState = RUNNING;
        isHoming = false;
    } else if (strstr(state, "Hold") == state) {
        LEDControl::LedColors::inStartupMode = false;
        machineState = PAUSED;
        isHoming = false;
    } else if (strstr(state, "Idle") == state) {
        // Only process Idle state if we're not in startup mode
        if (!LEDControl::LedColors::inStartupMode) {
            if (isHoming) {
                justFinishedHoming = true;
                isHoming = false;
                machineState = IDLE;
            } else if (machineState == HOMING) {
                machineState = IDLE;
            } else {
                machineState = IDLE;
            }
        }
    } else if (strstr(state, "Alarm") == state && enableAlarmCheck) {
        machineState = ALARM;
        alarm14Active = _alarm14;
        isHoming = false;
    } else if (strstr(state, "Jog") == state) {
        machineState = JOGGING;
        isHoming = false;
        LEDControl::LedColors::inStartupMode = false;
    } else if (strstr(state, "Home") == state) {
        machineState = HOMING;
        isHoming = true;
        justFinishedHoming = false;
    }

    updateLEDs();
}