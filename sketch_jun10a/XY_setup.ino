#include <AccelStepper.h>
#include <MultiStepper.h>



#define LINE_BUFFER_LENGTH 1024

const int stepsPerRevolution = 2048;


AccelStepper stepper1(AccelStepper::FULL4WIRE, 2, 4, 3, 5);
AccelStepper stepper2(AccelStepper::FULL4WIRE, 6, 8, 7, 9);
MultiStepper steppers;

struct point {
  float x;
  float y;
  float z;
};

struct point actuatorPos;

float StepInc = 1;
int StepDelay = 0;
int LineDelay = 10;
int penDelay = 50;

float StepsPerMillimeterX = 18.6;
float StepsPerMillimeterY = 18.6;

float Xmin = 0;
float Xmax = 150;
float Ymin = 0;
float Ymax = 150;
float Zmin = 0;
float Zmax = 1;

float Xpos = Xmin;
float Ypos = Ymin;
float Zpos = Zmax;
