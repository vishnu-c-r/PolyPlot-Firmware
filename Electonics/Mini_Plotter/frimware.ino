#include <Servo.h>
#include <AccelStepper.h>
#include <MultiStepper.h>

AccelStepper stepper1(AccelStepper::FULL4WIRE, 2, 4, 3, 5);
AccelStepper stepper2(AccelStepper::FULL4WIRE, 6, 8, 7, 9);
MultiStepper steppers;

#define LINE_BUFFER_LENGTH 1024

const int penZUp = 40;
const int penZDown = 80;

const int penServoPin = 15;

const int stepsPerRevolution = 2048;

Servo penServo;

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

boolean verbose = true;
long positions[2]; // Array of desired stepper positions

void setup() {
  Serial.begin(115200);

  // Configure each stepper
  stepper1.setMaxSpeed(300);
  stepper1.setAcceleration(400);
  stepper1.setCurrentPosition(0);
  stepper2.setMaxSpeed(300);
  stepper2.setAcceleration(400);
  stepper2.setCurrentPosition(0);
  steppers.addStepper(stepper1);
  steppers.addStepper(stepper2);

  penServo.attach(penServoPin);
  penServo.write(penZUp);
  delay(100);

  Serial.println(" Fab Plotter alive and kicking!");
  Serial.print("X range is from ");
  Serial.print(Xmin);
  Serial.print(" to ");
  Serial.print(Xmax);
  Serial.println(" mm.");
  Serial.print("Y range is from ");
  Serial.print(Ymin);
  Serial.print(" to ");
  Serial.print(Ymax);
  Serial.println(" mm.");
}

void loop() {
  delay(200);
  char line[LINE_BUFFER_LENGTH];
  char c;
  int lineIndex;
  bool lineIsComment, lineSemiColon;

  lineIndex = 0;
  lineSemiColon = false;
  lineIsComment = false;

  while (1) {
    while (Serial.available() > 0) {
      c = Serial.read();
      if ((c == '\n') || (c == '\r')) {
        if (lineIndex > 0) {
          line[lineIndex] = '\0';
          if (verbose) {
            Serial.print("Received : ");
            Serial.println(line);
          }
          processIncomingLine(line, lineIndex);
          lineIndex = 0;
        } else {
        }
        lineIsComment = false;
        lineSemiColon = false;
        Serial.println("ok");
      } else {
        if ((lineIsComment) || (lineSemiColon)) {
          if (c == ')')
            lineIsComment = false;
        } else {
          if (c <= ' ') {
          } else if (c == '/') {
          } else if (c == '(') {
            lineIsComment = true;
          } else if (c == ';') {
            lineSemiColon = true;
          } else if (lineIndex >= LINE_BUFFER_LENGTH - 1) {
            Serial.println("ERROR - lineBuffer overflow");
            lineIsComment = false;
            lineSemiColon = false;
          } else if (c >= 'a' && c <= 'z') {
            line[lineIndex++] = c - 'a' + 'A';
          } else {
            line[lineIndex++] = c;
          }
        }
      }
    }
  }
}

void processIncomingLine(char *line, int charNB) {
  int currentIndex = 0;
  char buffer[64];
  struct point newPos;

  newPos.x = 0.0;
  newPos.y = 0.0;

  while (currentIndex < charNB) {
    switch (line[currentIndex++]) {
      case 'U':
        penUp();
        break;
      case 'D':
        penDown();
        break;
      case 'G':
        buffer[0] = line[currentIndex++];
        buffer[1] = line[currentIndex++];
        buffer[2] = '\0';

        switch (atoi(buffer)) {
          case 00:
          case 01:
            char *indexX = strchr(line + currentIndex++, 'X');
            char *indexY = strchr(line + currentIndex++, 'Y');
            if (indexY <= 0) {
              newPos.x = atof(indexX + 1);
              newPos.y = actuatorPos.y;
            } else if (indexX <= 0) {
              newPos.y = atof(indexY + 1);
              newPos.x = actuatorPos.x;
            } else {
              newPos.y = atof(indexY + 1);
              indexY = NULL;
              newPos.x = atof(indexX + 1);
            }
            drawLine(newPos.x, newPos.y);
            actuatorPos.x = newPos.x;
            actuatorPos.y = newPos.y;
            break;
        }
        break;
      case 'M':
        buffer[0] = line[currentIndex++];
        buffer[1] = line[currentIndex++];
        //buffer[2] = line[currentIndex++];
        buffer[2] = '\0';
        switch (atoi(buffer)) {
          case 03:
            {
              char *indexS = strchr(line + currentIndex++, 'S');
              float Spos = atof(indexS + 1);
              if (Spos == 123) {
                penDown();
              }
              if (Spos == 000) {
                penUp();
              }
              break;
            }
          case 114:
            Serial.print("Absolute position : X = ");
            Serial.print(actuatorPos.x);
            Serial.print("  -  Y = ");
            Serial.println(actuatorPos.y);
            break;
          default:
            Serial.print("Command not recognized : M");
            Serial.println(buffer);
        }
    }
  }
}

void drawLine(float x1, float y1) {
  if (verbose) {
    Serial.print("fx1, fy1: ");
    Serial.print(x1);
    Serial.print(",");
    Serial.print(y1);
    Serial.println("");
  }

  if (x1 >= Xmax) {
    x1 = Xmax;
  }
  if (x1 <= Xmin) {
    x1 = Xmin;
  }
  if (y1 >= Ymax) {
    y1 = Ymax;
  }
  if (y1 <= Ymin) {
    y1 = Ymin;
  }

  if (verbose) {
    Serial.print("Xpos, Ypos: ");
    Serial.print(Xpos);
    Serial.print(",");
    Serial.print(Ypos);
    Serial.println("");
  }

  if (verbose) {
    Serial.print("x1, y1: ");
    Serial.print(x1);
    Serial.print(",");
    Serial.print(y1);
    Serial.println("");
  }

  x1 = (int)(x1 * StepsPerMillimeterX);
  y1 = (int)(y1 * StepsPerMillimeterY);

  mov(x1, y1);

  float x0 = Xpos;
  float y0 = Ypos;

  long dx = abs(x1 - x0);
  long dy = abs(y1 - y0);
  int sx = x0 < x1 ? StepInc : -StepInc;
  int sy = y0 < y1 ? StepInc : -StepInc;

  long i;
  long over = 0;

  if (dx > dy) {
    for (i = 0; i < dx; ++i) {
      over += dy;
      if (over >= dx) {
        over -= dx;
      }
      delay(StepDelay);
    }
  } else {
    for (i = 0; i < dy; ++i) {
      over += dx;
      if (over >= dy) {
        over -= dy;
      }
      delay(StepDelay);
    }
  }

  if (verbose) {
    Serial.print("dx, dy:");
    Serial.print(dx);
    Serial.print(",");
    Serial.print(dy);
    Serial.println("");
  }

  if (verbose) {
    Serial.print("Going to (");
    Serial.print(x0);
    Serial.print(",");
    Serial.print(y0);
    Serial.println(")");
  }

  delay(LineDelay);

  Xpos = x1;
  Ypos = y1;
}

void penUp() {
  penServo.write(penZUp);
  delay(LineDelay);
  Zpos = Zmax;
  if (verbose) {
    Serial.println("Pen up!");
  }
}

void penDown() {
  penServo.write(penZDown);
  delay(LineDelay);
  Zpos = Zmin;
  if (verbose) {
    Serial.println("Pen down.");
  }
}

void mov(long x, long y) {
  positions[0] = x + y;
  positions[1] = x - y;
  steppers.moveTo(positions);
  steppers.runSpeedToPosition();
}