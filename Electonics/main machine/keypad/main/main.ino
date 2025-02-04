#include <Arduino.h>
#include "GrblParserC.h"
#include <Adafruit_NeoPixel.h>
#include "LedConfig.hpp"  // Update to .hpp extension

// Forward declare the ButtonState struct
struct ButtonState;

// Function prototypes
void updateButtonState(ButtonState& btn, bool currentRead);
void handleButtons();
void updateLEDs();

//controller used: Attiny1614
//pinout: https://www.microchip.com/wwwproducts/en/ATtiny1614

// Pin definitions (using existing ones)
#define BUTTON_UP 9
#define BUTTON_RIGHT 8  
#define BUTTON_DOWN 1
#define BUTTON_LEFT 0
#define BUTTON_PLAYPAUSE 3
#define NEOPIXEL_PIN 10

// Configurable settings
#define JOG_FEEDRATE 10000      // Feedrate for jogging in mm/min
#define NUM_PIXELS 5          // Number of NeoPixels

// Create Neopixel object
Adafruit_NeoPixel pixels(NUM_PIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// Add brightness control
uint8_t currentBrightness = DEFAULT_BRIGHTNESS;

// Machine state enumeration
enum MachineState { RUNNING, PAUSED, IDLE, JOGGING, ALARM, COMPLETE };
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
char current_machine_state[20] = "Unknown";  // Add this variable definition

// Add these declarations near the top of the file after the #include statements
void fnc_send_play_command();
void fnc_send_pause_command();
void fnc_send_cancel_command();
void fnc_send_home_command();
extern char current_machine_state[20];

// Add these constants after other #define statements
#define BUTTON_HOLD_DELAY 500        // Default hold delay for jog buttons
#define HOME_HOLD_DELAY 1000         // Longer hold delay for home command
#define SHORT_JOG_DISTANCE 1     // Distance for single click (mm)
#define LONG_JOG_DISTANCE 1000   // Distance for continuous jog (mm)

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

void setup() {
    // Initialize primary UART communication with proper pin swap for ATtiny1614
    Serial.begin(115200);
    delay(1000);  // Give serial time to stabilize

    // Initialize buttons as inputs with pull-up resistors
    pinMode(BUTTON_UP, INPUT_PULLUP);
    pinMode(BUTTON_RIGHT, INPUT_PULLUP);
    pinMode(BUTTON_DOWN, INPUT_PULLUP);
    pinMode(BUTTON_LEFT, INPUT_PULLUP);
    pinMode(BUTTON_PLAYPAUSE, INPUT_PULLUP);

    // Initialize Neopixels with test pattern
    pixels.begin();
    pixels.clear();
    pixels.setBrightness(DEFAULT_BRIGHTNESS);
     
    // Test pattern - flash all LEDs white
    for (int i = 0; i < NUM_PIXELS; i++) {
        pixels.setPixelColor(i, pixels.Color(255, 255, 255));
    }
    pixels.show();
    delay(500);
    pixels.clear();
    pixels.show();
    
    LEDControl::LedColors::init(pixels);
    
    // Set initial LED state to IDLE
    machineState = IDLE;
    for (int i = 0; i < NUM_PIXELS; i++) {
        pixels.setPixelColor(i, LEDControl::LedColors::COLOR_IDLE);
    }
    pixels.show();

    // Wait for FluidNC to be ready
    fnc_wait_ready();
    
    // Configure faster status reporting
    fnc_send_line("$Report/Interval=50", 100);
    delay(100);  // Wait for setting to take effect
    
    // Send initial homing command
    fnc_send_line("$H", 100);

    // Wait for homing to complete
    delay(1000);  // Give time for homing to start
}

// Add this function after setup()
void handleSerialData() {
    while (Serial.available()) {
        char inChar = (char)Serial.read();
        
        // Add character to buffer if not newline
        if (inChar != '\n') {
            inputBuffer += inChar;
        }
        // If newline received, set flag
        else {
            messageComplete = true;
        }
    }

    // Process complete message
    if (messageComplete) {
        // Process the message here
        // You can add your message handling logic
        
        // Clear buffer for next message
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
            // Handle long press for home
            fnc_send_home_command();
            playPauseButton.longPressCommandSent = true;
        } else if (!playPauseButton.isHeld) {
            // Handle short press for play/pause
            if (machineState == RUNNING || machineState == JOGGING) {
                // Send pause command directly
                fnc_putchar((uint8_t)FeedHold);
            } else if (machineState == PAUSED || machineState == IDLE) {
                // Send cycle start command directly
                fnc_putchar((uint8_t)CycleStart);
            }
            // Clear the press state
            playPauseButton.isPressed = false;
        }
        return;  // Exit after handling play/pause button
    }

    // Rest of button handling for jog commands
    // ...existing code...
    // Handle LED updates based on state
    if (machineState == RUNNING || machineState == JOGGING) {
        // During running/jogging state, set button LEDs
        uint32_t baseColor = (machineState == JOGGING) ? 
                            LEDControl::LedColors::COLOR_JOG : 
                            LEDControl::LedColors::COLOR_RUNNING;
        
        pixels.setPixelColor(LED_UP, upButton.currentState ? LEDControl::LedColors::COLOR_PRESSED : baseColor);
        pixels.setPixelColor(LED_RIGHT, rightButton.currentState ? LEDControl::LedColors::COLOR_PRESSED : baseColor);
        pixels.setPixelColor(LED_DOWN, downButton.currentState ? LEDControl::LedColors::COLOR_PRESSED : baseColor);
        pixels.setPixelColor(LED_LEFT, leftButton.currentState ? LEDControl::LedColors::COLOR_PRESSED : baseColor);
        pixels.setPixelColor(LED_PLAYPAUSE, baseColor);
    }

    pixels.show();

    // Only process jog commands when not paused
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
            if (!upButton.currentState) {
                upButton.longPressCommandSent = false;
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
            if (!rightButton.currentState) {
                rightButton.longPressCommandSent = false;
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
            if (!downButton.currentState) {
                downButton.longPressCommandSent = false;
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
            if (!leftButton.currentState) {
                leftButton.longPressCommandSent = false;
            }
        }
    }
}

void updateLEDs() {
    static unsigned long fadeTimer = 0;
    static int fadeValue = 0;
    static int fadeStep = 5;
    uint32_t fadeColor = 0;  // Declare at top level

    // Clear all LEDs first
    pixels.clear();

    switch (machineState) {
        case IDLE:
            // Cyan color for idle
            for (int i = 0; i < NUM_PIXELS; i++) {
                pixels.setPixelColor(i, LEDControl::LedColors::COLOR_IDLE);
            }
            break;

        case RUNNING:
            // Purple for running
            for (int i = 0; i < NUM_PIXELS; i++) {
                pixels.setPixelColor(i, LEDControl::LedColors::COLOR_RUNNING);
            }
            break;

        case JOGGING:
            // Blue for jogging
            for (int i = 0; i < NUM_PIXELS; i++) {
                pixels.setPixelColor(i, LEDControl::LedColors::COLOR_JOG);
            }
            break;

        case PAUSED:
            // Update fade timing for orange pulsing
            if (millis() - fadeTimer > 50) {
                fadeTimer = millis();
                fadeValue += fadeStep;
                if (fadeValue >= 255) {
                    fadeValue = 255;
                    fadeStep = -5;
                } else if (fadeValue <= 0) {
                    fadeValue = 0;
                    fadeStep = 5;
                }
            }
            
            // Calculate fade color (orange)
            fadeColor = pixels.Color(
                map(fadeValue, 0, 255, 0, 255),    // Red
                map(fadeValue, 0, 255, 0, 100),    // Green
                0                                   // Blue
            );
            
            for (int i = 0; i < NUM_PIXELS; i++) {
                pixels.setPixelColor(i, fadeColor);
            }
            break;

        case ALARM:
            // Red for alarm state
            for (int i = 0; i < NUM_PIXELS; i++) {
                pixels.setPixelColor(i, LEDControl::LedColors::COLOR_ERROR);
            }
            break;

        case COMPLETE:
            // Green for completion
            for (int i = 0; i < NUM_PIXELS; i++) {
                pixels.setPixelColor(i, LEDControl::LedColors::COLOR_COMPLETE);
            }
            break;

        default:
            // Set default color (e.g., off)
            for (int i = 0; i < NUM_PIXELS; i++) {
                pixels.setPixelColor(i, LEDControl::LedColors::COLOR_OFF);
            }
            break;
    }

    // Show pressed button states
    if (machineState == RUNNING || machineState == JOGGING) {
        if (upButton.currentState) pixels.setPixelColor(LED_UP, LEDControl::LedColors::COLOR_PRESSED);
        if (rightButton.currentState) pixels.setPixelColor(LED_RIGHT, LEDControl::LedColors::COLOR_PRESSED);
        if (downButton.currentState) pixels.setPixelColor(LED_DOWN, LEDControl::LedColors::COLOR_PRESSED);
        if (leftButton.currentState) pixels.setPixelColor(LED_LEFT, LEDControl::LedColors::COLOR_PRESSED);
    }

    pixels.show();
}

// Callback function for machine state changes
void show_state(const char* state) {
    strncpy(current_machine_state, state, sizeof(current_machine_state));
    
    // Update machine state with more granular parsing
    if (strstr(state, "Run") == state) {
        machineState = RUNNING;
        alarm14Active = false;
    } else if (strstr(state, "Hold") == state) {
        machineState = PAUSED;
    } else if (strstr(state, "Idle") == state) {
        machineState = IDLE;
        alarm14Active = false;
    } else if (strstr(state, "Alarm") == state) {
        machineState = ALARM;
        if (_alarm14) {
            alarm14Active = true;
        }
    } else if (strstr(state, "Jog") == state) {
        machineState = JOGGING;
    } else if (strstr(state, "Home") == state) {
        machineState = COMPLETE;
        alarm14Active = false;
    }
    
    // Force immediate LED update to reflect new state
    updateLEDs();
}