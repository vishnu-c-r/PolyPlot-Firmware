#ifndef PENCONTROL_H
#define PENCONTROL_H

#include <Servo.h>

extern const int penZUp;
extern const int penZDown;
extern const int penServoPin;

extern Servo penServo;

extern float Zpos;
extern float Zmax;
extern float Zmin;

extern int penDelay;
extern boolean verbose;

void setupPen();
void penUp();
void penDown();

#endif
