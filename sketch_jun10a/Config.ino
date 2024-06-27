#include<XY_setup>
void config_setup(){

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