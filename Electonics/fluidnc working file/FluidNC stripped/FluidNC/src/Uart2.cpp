#include "Serial2.h"
#include <Arduino.h>

void initSerial2() {

    // Configure the UART2 port (Serial2)
    Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

    // Give some time for the serial to initialize
    delay(10000);

    Serial.println("initialised");

    // Wait for "ok" message from Serial2
    if (waitForModMessage(10000)) { // 5000ms timeout
        Serial.println("Received multipen module from Serial2");
        Serial2.println("ok");
    } else {
        Serial.println("Timeout waiting for 'ok' from Serial2");
    }
}

void sendMessage(const char* message) {
    // Send the message through Serial2
    Serial2.println(message);
    Serial.println(message);
    while (waitForOkMessage(10000))
    {
        Serial.println("waiting");
    }
    // Print the message to the serial monitor for debugging
    Serial.println("Sent");
    if (strcmp(message, "M28") != 1) {
        while (waitForOkMessage(10000))
        {
            Serial.println("waiting");
        }
        Serial.println("ok received");
        return;
    }
}

bool waitForOkMessage(unsigned long timeout) {
    unsigned long startMillis = millis();
    String receivedMessage = "";

    while (millis() - startMillis < timeout) {
        while (Serial2.available()) {
            char ch = Serial2.read();
            receivedMessage += ch;
            if (receivedMessage.endsWith("ok")) {
                return false;
            }
        }
    }
    return true;
}

bool waitForModMessage(unsigned long timeout) {
    unsigned long startMillis = millis();
    String receivedMessage = "";

    while (millis() - startMillis < timeout) {
        while (Serial2.available()) {
            char ch = Serial2.read();
            receivedMessage += ch;
            if (receivedMessage.endsWith("multipen module")) {
                return true;
            }
        }
    }
    return false;
}
