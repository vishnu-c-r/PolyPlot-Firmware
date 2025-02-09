#include <Arduino.h>
#include <AccelStepper.h>

// Define stepper motor control pins
#define STEPPER_PIN1 0
#define STEPPER_PIN2 1
#define STEPPER_PIN3 2
#define STEPPER_PIN4 3
#define LIMIT_SWITCH_PIN 5
#define LED_PIN 4

bool handshakeComplete = false;
String receivedData = "";

// Create instance of the stepper motor
AccelStepper stepper(AccelStepper::FULL4WIRE, STEPPER_PIN1, STEPPER_PIN3, STEPPER_PIN2, STEPPER_PIN4);

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
      
      if (receivedData == "M28") {
        homeStepper();
        delay(200);
        Serial.println("ok");
      } else {
        Serial.print("Received step count: ");
        Serial.println(receivedData);
        int stepCount = receivedData.toInt();
        moveToPosition(stepCount);
        delay(200);
        Serial.println("ok");
      }
      
      // Clear the received data for the next command
      receivedData = "";
    }
  }
}

void homeStepper() {
  stepper.setSpeed(400);  // Set speed in the positive direction
  while (digitalRead(LIMIT_SWITCH_PIN) == HIGH) {
    digitalWrite(LED_PIN, HIGH);
    stepper.runSpeed();  // Move stepper motor towards home position
  }
  stepper.setSpeed(-400);
  while (digitalRead(LIMIT_SWITCH_PIN) == LOW) {
    digitalWrite(LED_PIN, LOW);
    stepper.runSpeed();
  }
  stepper.stop();
  stepper.setCurrentPosition(0);
}

void moveToPosition(int position) {
  stepper.moveTo(position);  // Move to the specified physical position
  stepper.runToPosition();   // Blocking call to move to the target position
  Serial.println("ok");
}
