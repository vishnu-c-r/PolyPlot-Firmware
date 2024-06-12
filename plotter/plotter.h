#ifndef PLOTTER_H
#define PLOTTER_H

#include <Servo.h>
#include <AccelStepper.h>
#include <MultiStepper.h>
#include "PenControl.h"
#include "Movement.h"
#include "Commands.h"

#define X_Limit 9
#define Y_Limit 14
#define LED 28

#define LINE_BUFFER_LENGTH 1024

extern AccelStepper stepper1;
extern AccelStepper stepper2;
extern MultiStepper steppers;

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

struct point {
  float x;
  float y;
  float z;
};

extern struct point actuatorPos;

void setupPlotter();
void loopPlotter();
void processCommands();
void processIncomingLine(char *line, int charNB);
void drawLine(float x1, float y1);
void mov(long x, long y);
void home();

#endif
