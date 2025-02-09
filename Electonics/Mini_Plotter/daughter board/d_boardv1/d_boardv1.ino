#include <Arduino.h> 
#include <AccelStepper.h> 
// Define stepper motor control pins 
#define STEPPER_PIN1 0 
#define STEPPER_PIN2 1 
#define STEPPER_PIN3 2 
#define STEPPER_PIN4 3 
#define LIMIT_SWITCH_PIN 5 
#define LED_PIN 4 

#define PENPOS1 0
#define PENPOS2 1028+256
#define PENPOS3 2560+50
#define PENPOS4 3840
#define PENPOS5 5120
#define PENPOS6 6400
#define PENPOS7 7680
#define PENPOS8 8960

#define offset 256
 
String receivedData = ""; 


// Create instance of the stepper motor 
AccelStepper stepper(AccelStepper::FULL4WIRE, STEPPER_PIN1, STEPPER_PIN3, STEPPER_PIN2, STEPPER_PIN4); 
 
const int stepsPerRevolution = 2048; // Change according to your stepper motor 
const int positions = 8; 
const int stepsPerPosition = stepsPerRevolution / positions; 
int currentPosition = 0; 
 
void setup() { 
  // Set pin modes 
  pinMode(LIMIT_SWITCH_PIN, INPUT_PULLUP); 
  pinMode(LED_PIN, OUTPUT); 
 
  // Initialize UART communication
   Serial.swap(1);  
  Serial.begin(9600); 


 
  // Set the maximum speed and acceleration for the stepper motor 
  stepper.setMaxSpeed(600); 
  stepper.setAcceleration(300); 

  delay(1000); 
  Serial.println("Ready to receive commands."); 
 
  // Home the stepper motor at startup 
  homeStepper(); 
} 
 
void loop() {

 // Check if data is available to read 
  if (Serial.available() > 0) { 
    // Read the incoming data 
    char incomingChar = Serial.read(); 
    receivedData += incomingChar; 
 
    // Check if the incoming data ends with a newline character (indicating the end of a command) 
    if (incomingChar == '\n') { 
      receivedData.trim(); // Remove any leading or trailing whitespace 
 
      // Check for specific commands 

      receivedData.toUpperCase();

      if (receivedData == "HAI") { 
        digitalWrite(LED_PIN, HIGH); // Turn on the LED 
        Serial.println("LED turned ON"); 
      } 
      else if (receivedData == "HELLO") { 
        digitalWrite(LED_PIN, LOW); // Turn off the LED 
        Serial.println("LED turned OFF");
       
      } 
      else if(receivedData == "M03S1")
      {
        moveToPosition(PENPOS1);
        

      }

       else if(receivedData == "M03S2")
      {

        moveToPosition(PENPOS2);

      }

       else if(receivedData == "M03S3")
      {
        moveToPosition(PENPOS3);

      }

       else if(receivedData == "M03S4")
      {
        moveToPosition(PENPOS4);

      }

       else if(receivedData == "M03S5")
      {
        moveToPosition(PENPOS5);

      }

       else if(receivedData == "M03S6")
      {
        moveToPosition(PENPOS6);

      }

       else if(receivedData == "M03S7")
      {
        moveToPosition(PENPOS7);

      }

       else if(receivedData == "M03S8")
      {
        moveToPosition(PENPOS8);

      }
       else if(receivedData == "M28")
      {
        moveToPosition(PENPOS8);

      }

      else { 
        Serial.println("Unknown command"); 
      } 
 
      // Clear the received data for the next command 
      receivedData = ""; 
    } 
  } 



} 
 
void homeStepper() { 
  stepper.setSpeed(400); // Set speed in the negative direction 
  while (digitalRead(LIMIT_SWITCH_PIN) == HIGH) { 
    stepper.runSpeed(); // Move stepper motor towards home position 
  } 
  delay(500); 
  stepper.setSpeed(-400);  
  while (digitalRead(LIMIT_SWITCH_PIN) == LOW) { 
    stepper.runSpeed(); // Move stepper motor towards home position 
  } 
  stepper.stop(); 
  stepper.setCurrentPosition(0); // Set the current position as home 
  currentPosition = 0; 
} 
 
void moveToPosition(int steps) { 

  

  stepper.moveTo(steps+offset); 
  stepper.runToPosition(); // Blocking call to move to the target position 
  currentPosition = steps; 
   Serial.println("ok");
 
 
}
