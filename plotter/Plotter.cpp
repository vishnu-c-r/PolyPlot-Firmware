#include "Plotter.h"

void setupPlotter() {
  Serial.begin(115200);

  pinMode(X_Limit, INPUT_PULLUP);
  pinMode(Y_Limit, INPUT_PULLUP);
  pinMode(LED, OUTPUT);

  stepper1.setMaxSpeed(300);
  stepper1.setAcceleration(400);
  stepper1.setCurrentPosition(0);
  stepper2.setMaxSpeed(300);
  stepper2.setAcceleration(400);
  stepper2.setCurrentPosition(0);
  steppers.addStepper(stepper1);
  steppers.addStepper(stepper2);

  setupPen();

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

  home();
}

void loopPlotter() {
  processCommands();
}
