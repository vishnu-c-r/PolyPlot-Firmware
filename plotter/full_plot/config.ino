#include "config.h"

const int penZUp = 80;
const int penZDown = 40;
const int penServoPin = 16;

const int stepsPerRevolution = 2048;

Servo penServo;

struct point actuatorPos = {0, 0, 0};

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
