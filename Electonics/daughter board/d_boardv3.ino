#include <Arduino.h>
#include <AccelStepper.h>

// Define stepper motor control pins
#define STEPPER_PIN1 0
#define STEPPER_PIN2 1
#define STEPPER_PIN3 2
#define STEPPER_PIN4 3
#define LIMIT_SWITCH_PIN 5
#define LED_PIN 4

#define GEAR_RATIO 5  // Gear ratio

bool isAtHome = false;
bool handshakeComplete = false;
int calibrationCounter = 0;
const int calibrationThreshold = 5;

String receivedData = "";

// Create instance of the stepper motor
AccelStepper stepper(AccelStepper::FULL4WIRE, STEPPER_PIN1, STEPPER_PIN3, STEPPER_PIN2, STEPPER_PIN4);

const int stepsPerRevolution = 2048;  // Change according to your stepper motor
const int positions = 8;
const int stepsPerPosition = stepsPerRevolution / positions;
int currentPosition = 0;

int penOfsets[8] = {350,320,270,280,365,410,485,520};

void setup() {
  // Set pin modes
  pinMode(LIMIT_SWITCH_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  // Initialize UART communication
  Serial.swap(1);
  Serial.begin(9600);
  delay(1500);
  // Set the maximum speed and acceleration for the stepper motor
  stepper.setMaxSpeed(600);
  stepper.setAcceleration(500);
  delay(2000);
  homeStepper();  // Home the stepper motor at startup
  while (isAtHome == false) {}
  
  // Start handshake sequence
  while (!handshakeComplete) {
    Serial.println("multipen module");
    delay(500);
    if (Serial.available() > 0) {
      String response = Serial.readStringUntil('\n');
      response.trim();
      if (response == "ok") {
        handshakeComplete = true;
        Serial.println("Handshake complete. Ready to receive commands.");
      }
    }
  }
}

void loop() {
  // Check if data is available to read
  if (handshakeComplete && Serial.available() > 0) {
    // Read the incoming data
    char incomingChar = Serial.read();
    receivedData += incomingChar;
    // Check if the incoming data ends with a newline character (indicating the end of a command)
    if (incomingChar == '\n') {
      receivedData.trim();  // Remove any leading or trailing whitespace

      // Convert to uppercase for easier comparison
      receivedData.toUpperCase();
      Serial.print("Received data: ");
      Serial.println(receivedData);
      calibrationCounter++;
      if (calibrationCounter >= calibrationThreshold) {
        homeStepper();  // Recalibrate the stepper motor
        calibrationCounter = 0;
      }
      if (receivedData == "M03S1")
        moveToPosition(getPenPosition(1));
      else if (receivedData == "M03S2")
        moveToPosition(getPenPosition(2));
      else if (receivedData == "M03S3")
        moveToPosition(getPenPosition(3));
      else if (receivedData == "M03S4")
        moveToPosition(getPenPosition(4));
      else if (receivedData == "M03S5")
        moveToPosition(getPenPosition(5));
      else if (receivedData == "M03S6")
        moveToPosition(getPenPosition(6));
      else if (receivedData == "M03S7")
        moveToPosition(getPenPosition(7));
      else if (receivedData == "M03S8")
        moveToPosition(getPenPosition(8));
      else if (receivedData == "M28")
        homeStepper();  // Run the homing sequence
      else
        Serial.println("Unknown command");

      // Clear the received data for the next command
      receivedData = "";
    }
  }
}

void homeStepper() {
  stepper.setSpeed(600);  // Set speed in the positive direction
  while (digitalRead(LIMIT_SWITCH_PIN) == HIGH)
    stepper.runSpeed();  // Move stepper motor towards home position
  stepper.setSpeed(-600);
  while (digitalRead(LIMIT_SWITCH_PIN) == LOW)
    stepper.runSpeed();
  stepper.stop();
  stepper.setCurrentPosition(0);  // Set the current position as home
  currentPosition = 0;
  isAtHome = true;
}

void moveToPosition(int position) {
  stepper.moveTo(position);  // Move to the specified physical position
  stepper.runToPosition();   // Blocking call to move to the target position
  currentPosition = position;
  isAtHome = false;
  Serial.println("ok");
}

int getPenPosition(int penNumber) {
  return ((penNumber - 1) * (stepsPerRevolution / positions) * GEAR_RATIO) + penOfsets[penNumber - 1];
}
