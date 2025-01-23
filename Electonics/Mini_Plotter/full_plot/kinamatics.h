#ifndef KINAMATICS_H
#define KINAMATICS_H
/// Header file of kinamatics of the core XY

#include "config.h"
#include <AccelStepper.h>
#include <MultiStepper.h>

extern AccelStepper stepper1;
extern AccelStepper stepper2;
extern MultiStepper steppers;

void setupMovement();
void drawLine(float x1, float y1);
void mov(long x, long y);
void home();

#endif // 
