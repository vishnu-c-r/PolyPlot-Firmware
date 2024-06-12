#ifndef MOVEMENT_H
#define MOVEMENT_H

#include <AccelStepper.h>
#include <MultiStepper.h>

extern AccelStepper stepper1;
extern AccelStepper stepper2;
extern MultiStepper steppers;

extern float StepsPerMillimeterX;
extern float StepsPerMillimeterY;

extern float Xmin;
extern float Xmax;
extern float Ymin;
extern float Ymax;

extern float Xpos;
extern float Ypos;
extern float StepInc;
extern int StepDelay;
extern int LineDelay;

extern long positions[2];

void setupMovement();
void drawLine(float x1, float y1);
void mov(long x, long y);
void home();

#endif
