#include "switch.h"
#include "poll.h"
#include "remote.h"
#include "wifi.h"

#include <Arduino.h>

int poll(int* source) {
  *source = SOURCE_SWITCH;
  if (int i = poll_switch()) {
    return i;
  }
  *source = SOURCE_REMOTE;
  if (int i = poll_remote()) {
    return i;
  }
  *source = SOURCE_WIFI;
  if (int i = poll_wifi()) {
    return i;
  }
  *source = SOURCE_NONE;
  return 0;
}


void initialize_all() {
  initialize_switch();
  initialize_remote();
  initialize_wifi();
}
