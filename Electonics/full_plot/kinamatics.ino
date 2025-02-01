#include "kinamatics.h"
#include <Arduino.h>

AccelStepper stepper1(AccelStepper::FULL4WIRE, 5, 4, 3, 2);
AccelStepper stepper2(AccelStepper::FULL4WIRE, 22, 19, 18,17);
MultiStepper steppers;

void setupMovement() {
    // Configure each stepper
    stepper1.setMaxSpeed(300);
    stepper1.setAcceleration(400);
    stepper1.setCurrentPosition(0);
    stepper2.setMaxSpeed(300);
    stepper2.setAcceleration(400);
    stepper2.setCurrentPosition(0);
    steppers.addStepper(stepper1);
    steppers.addStepper(stepper2);
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

void mov(long x, long y) {
    positions[0] = -(x + y);
    positions[1] = -(x - y);
    steppers.moveTo(positions);
    steppers.runSpeedToPosition();
}

void home() {
  hs = 1;
   int sensorValue = analogRead(analogPin);
  

  
  // Print to the Serial Monitor
  Serial.print("sensor input: ");
  Serial.print(sensorValue);
  Serial.println(" units");

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
    hs = 0;

    positions[0] = 0;
    positions[1] = 0;
}
