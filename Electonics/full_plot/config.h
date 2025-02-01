#ifndef CONFIG_H
#define CONFIG_H

#include <Servo.h>
#include <AccelStepper.h>
#include <MultiStepper.h>

// Pin definitions
#define X_Limit 9
#define Y_Limit 14
#define LED 28

// Buffer length for commands
#define LINE_BUFFER_LENGTH 1024

// Pen positions
extern const int penZUp;
extern const int penZDown;
extern const int penServoPin;
extern const int analogPin;
extern const int stepsPerRevolution;

extern Servo penServo;

struct point {
  float x;
  float y;
  float z;
};

extern struct point actuatorPos;

// Movement configurations
extern float StepInc;
extern int StepDelay;
extern int LineDelay;
extern int penDelay;

extern float StepsPerMillimeterX;
extern float StepsPerMillimeterY;

extern float Xmin;
extern float Xmax;
extern float Ymin;
extern float Ymax;
extern float Zmin;
extern float Zmax;

extern float Xpos;
extern float Ypos;
extern float Zpos;

extern boolean verbose;
extern long positions[2];
extern boolean hs;
#endif // CONFIG_H
