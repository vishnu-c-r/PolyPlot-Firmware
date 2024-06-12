#include "Plotter.h"

AccelStepper stepper1(AccelStepper::FULL4WIRE, 2, 4, 3, 5);
AccelStepper stepper2(AccelStepper::FULL4WIRE, 17, 19, 18, 22);
MultiStepper steppers;

const int penZUp = 40;
const int penZDown = 80;
const int penServoPin = 16;

float StepInc = 1;
int StepDelay = 0;
int LineDelay = 10;
int penDelay = 10;

float StepsPerMillimeterX = 21.55;
float StepsPerMillimeterY = 21.55;

float Xmin = 0;
float Xmax = 210;
float Ymin = 0;
float Ymax = 300;
float Zmin = 0;
float Zmax = 1;

float Xpos = Xmin;
float Ypos = Ymin;
float Zpos = Zmax;

boolean verbose = true;
long positions[2];

struct point actuatorPos;

Servo penServo;

void setup() {
  setupPlotter();
}

void loop() {
  loopPlotter();
}

// Define setupPlotter(), loopPlotter(), and other functions in respective files
