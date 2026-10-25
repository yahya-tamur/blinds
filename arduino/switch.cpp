#include "switch.h"

#include <Arduino.h> 

int poll_switch() {
  if (digitalRead(BUTTON1) == LOW) {
    return 1;
  } else if (digitalRead(BUTTON2) == LOW){
    return -1;
  } else {
    return 0;
  }

}


void initialize_switch() {
  pinMode(BUTTON1, INPUT_PULLUP);
  pinMode(BUTTON2, INPUT_PULLUP);
}