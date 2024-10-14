// Serial2Communication.h
#pragma once

// Define UART2 pins
#define RXD2 21//new 21
#define TXD2 22//new 22

// Function declarations
void initSerial2();
void sendMessage(const char* message);
bool waitForOkMessage(unsigned long timeout);
bool waitForModMessage(unsigned long timeout);