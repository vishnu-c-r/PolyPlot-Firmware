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

// Add after other state declarations
bool inStartupPhase = true;
bool homingComplete = false;

// OPTIMIZATION: Add a dedicated flag for homing (instead of checking current_machine_state text)
bool isHoming = false;

// Add near other state variables
bool justFinishedHoming = false;  // Add this flag

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
    
    // Set initial LED state to IDLE using COLOR_GREEN instead of non-existent COLOR_IDLE
    machineState = IDLE;
    for (int i = 0; i < NUM_PIXELS; i++) {
        pixels.setPixelColor(i, LEDControl::LedColors::COLOR_GREEN);
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
        if (inChar == '\n' || inputBuffer.length() >= REPORT_BUFFER_LEN - 1) {
            messageComplete = true;
        } else {
            inputBuffer += inChar;
        }
    }
    if (messageComplete) {
        // Process the message here
        Serial.print(F("Received: "));
        Serial.println(inputBuffer);
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
        // Use available constants:
        uint32_t baseColor = (machineState == JOGGING) ? LEDControl::LedColors::COLOR_GREEN : LEDControl::LedColors::COLOR_GREEN;
        // For pressed state, use COLOR_ORANGE instead of non-existent COLOR_PRESSED
        pixels.setPixelColor(LED_UP, upButton.currentState ? LEDControl::LedColors::COLOR_ORANGE : baseColor);
        pixels.setPixelColor(LED_RIGHT, rightButton.currentState ? LEDControl::LedColors::COLOR_ORANGE : baseColor);
        pixels.setPixelColor(LED_DOWN, downButton.currentState ? LEDControl::LedColors::COLOR_ORANGE : baseColor);
        pixels.setPixelColor(LED_LEFT, leftButton.currentState ? LEDControl::LedColors::COLOR_ORANGE : baseColor);
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

// Replace the updateLEDs() function with the following version:

void updateLEDs() {
    static MachineState prevState = machineState;
    static bool transitionActive = false;
    static unsigned long transitionStart = 0;
    const uint16_t TRANSITION_DURATION = 300;  // Short transition

    // Start transition when state changes (skip for ALARM)
    if ((machineState != prevState) && (machineState != ALARM)) {
        transitionActive = true;
        transitionStart = millis();
        prevState = machineState;
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
            case HOMING:  targetColor = pixels.Color(0, 255, 255); break; // Cyan
            default:      targetColor = LEDControl::LedColors::COLOR_OFF; break;
        }
        uint32_t fromColor = pixels.getPixelColor(0);
        uint32_t newColor;
        uint32_t elapsed = millis() - transitionStart;
        if (elapsed >= TRANSITION_DURATION) {
            transitionActive = false;
            newColor = targetColor;
        } else {
            uint8_t progress = (elapsed * 255UL) / TRANSITION_DURATION;
            newColor = interp(fromColor, targetColor, progress);
        }
        for (int i = 0; i < NUM_PIXELS; i++) {
            pixels.setPixelColor(i, newColor);
        }
        pixels.show();
        return;  // Skip state animations until transition finishes
    }

    // State-specific animations when not transitioning:
    if (machineState == IDLE) {
        if (justFinishedHoming) {
            // Special post-homing green blink
            LEDControl::LedColors::readyBlinkAnimation();
            if (LEDControl::LedColors::blinkCount >= 3) {  // After 3 blinks
                justFinishedHoming = false;  // Reset flag
                LEDControl::LedColors::blinkCount = 0;
            }
        } else {
            // Normal IDLE state display
            int arrowLEDs[4] = {LED_UP, LED_RIGHT, LED_DOWN, LED_LEFT};
            for (int i = 0; i < 4; i++) {
                pixels.setPixelColor(arrowLEDs[i], LEDControl::LedColors::COLOR_GREEN);
            }
            pixels.setPixelColor(LED_PLAYPAUSE, LEDControl::LedColors::COLOR_OFF);
        }
    } else if (machineState == RUNNING) {
        // RUNNING: arrow LEDs off; PLAY/PAUSE blinks (handled by runningAnimation)
        for (int i = 0; i < 4; i++) {
            pixels.setPixelColor(i, LEDControl::LedColors::COLOR_OFF);
        }
        LEDControl::LedColors::runningAnimation(false);
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
    strncpy(current_machine_state, state, sizeof(current_machine_state));
    
    // Parse incoming state and update machine state with proper transition handling
    if (strstr(state, "Run") == state) {
        machineState = RUNNING;
        alarm14Active = false;
        isHoming = false;
    } else if (strstr(state, "Hold") == state) {
        machineState = PAUSED;
        isHoming = false;
    } else if (strstr(state, "Idle") == state) {
        machineState = IDLE;
        if (isHoming) {
            justFinishedHoming = true;  // Trigger post-homing LED sequence
        }
        alarm14Active = false;
        isHoming = false;
    } else if (strstr(state, "Alarm") == state) {
        machineState = ALARM;
        alarm14Active = false; // or define and use a proper _alarm14 flag
        isHoming = false;
    } else if (strstr(state, "Jog") == state) {
        machineState = JOGGING;
        isHoming = false;
    } else if (strstr(state, "Home") == state) {
        machineState = HOMING;
        isHoming = true;
    }
    // Force immediate LED update to reflect new state
    updateLEDs();
}
