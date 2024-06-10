
#include <arduino.h>
#include <Servo.h>
#include <AccelStepper.h>
#include <MultiStepper.h>

AccelStepper stepper1(AccelStepper::FULL4WIRE, 2, 4, 3, 5);
AccelStepper stepper2(AccelStepper::FULL4WIRE, 17, 19, 18, 22);
MultiStepper steppers;



#define X_Limit 9
#define Y_Limit 14
#define LED 28



#define LINE_BUFFER_LENGTH 1024



const int penZUp = 40;
const int penZDown = 80;

const int penServoPin = 16;

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

void setup() {
  Serial.begin(115200);

  pinMode(X_Limit, INPUT_PULLUP);
  pinMode(Y_Limit, INPUT_PULLUP);
  pinMode(LED, OUTPUT);

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

  Serial.println(" Fab Plotter is Ready");
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


  // positions[0] = Xmax * StepsPerMillimeterX;
  // positions[1] = Ymax * StepsPerMillimeterY;
  // steppers.moveTo(positions);


  home();
}

void loop() {

  processCommands();
}

void processCommands() {

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
        Serial.println("OK");
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


  char *indexX;
  char *indexY;

  while (currentIndex < charNB) {
    switch (line[currentIndex++]) {
      case 'G':
        buffer[0] = line[currentIndex++];
        buffer[1] = line[currentIndex++];
        buffer[2] = '\0';

        switch (atoi(buffer)) {
          case 00:
          case 01:
            indexX = strchr(line + currentIndex++, 'X');
            indexY = strchr(line + currentIndex++, 'Y');
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

          case 28:
            home();
            break;
        }
        break;
      case 'M':
        buffer[0] = line[currentIndex++];
        buffer[1] = line[currentIndex++];
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

          default:
            Serial.print("Command not recognized : M");
            Serial.println(buffer);
        }
    }
  }
}

void drawLine(float x1, float y1) {
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
  positions[0] = -(x + y);
  positions[1] = -(x - y);
  steppers.moveTo(positions);
  steppers.runSpeedToPosition();
}

// void homingRoutine() {

//   mov(Xmax, Ymax);
//   mov(HOME_X, HOME_Y);

//   Serial.println("Homing complete !");
// }


void home() {

  positions[1] = Xmax * StepsPerMillimeterX;
  positions[0] = Xmax * StepsPerMillimeterX;
  steppers.moveTo(positions);


  while (!digitalRead(X_Limit)) {
    //digitalWrite(LED, HIGH);
    steppers.run();
  }


  positions[0] = Ymax * StepsPerMillimeterY;
  positions[1] = -(Ymax * StepsPerMillimeterY);
  steppers.moveTo(positions);


  while (!digitalRead(Y_Limit)) {
    //digitalWrite(LED, HIGH);
    steppers.run();
  }


  positions[0] = 0;
  positions[1] = 0;
}
