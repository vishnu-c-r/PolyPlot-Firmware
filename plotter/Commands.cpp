#include "Commands.h"
#include "Plotter.h"

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
