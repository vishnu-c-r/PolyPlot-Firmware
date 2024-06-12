#include "PenControl.h"

void setupPen() {
  penServo.attach(penServoPin);
  penServo.write(penZUp);
  delay(100);
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
