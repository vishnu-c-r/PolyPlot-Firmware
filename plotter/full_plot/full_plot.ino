#include <Servo.h>
#include <AccelStepper.h>
#include <MultiStepper.h>
#include "config.h"
#include "Z_axis.h"
#include "kinamatics.h"
#include "parser.h"

void setup() {
    Serial.begin(115200);

    pinMode(X_Limit, INPUT_PULLUP);
    pinMode(Y_Limit, INPUT_PULLUP);
    pinMode(LED, OUTPUT);

    setupMovement();
    setupPen();

    Serial.println("Fab Plotter is Ready");
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

void loop() {
    processCommands();
}