#include "switch.h"
#include "poll.h"

#include <Arduino.h>

// A position has to read the same for this long to count, so contact bounce
// while the switch is being moved doesn't register as several moves
const unsigned long SETTLE_MS = 30;

static int position = 0;     // last settled position: 1 up, -1 down, 0 off
static int last_read = 0;    // what the pins showed last time
static unsigned long read_since = 0;

static int read_position() {
  if (digitalRead(BUTTON1) == LOW) {
    return 1;
  } else if (digitalRead(BUTTON2) == LOW) {
    return -1;
  } else {
    return 0;
  }
}

// Reports moves of the switch rather than where it is: 1 or -1 when it's moved
// from off to up or down, CMD_CANCEL when it's moved back to off, 0 otherwise.
// Moved straight from one side to the other, it reports CMD_CANCEL first and
// the new side on the next call.
int poll_switch() {
  int now = read_position();
  if (now != last_read) {
    last_read = now;
    read_since = millis();
    return 0;
  }
  if (now == position || millis() - read_since < SETTLE_MS) {
    return 0;
  }
  if (position != 0 && now != 0) {
    position = 0;
    return CMD_CANCEL;
  }
  position = now;
  return now == 0 ? CMD_CANCEL : now;
}


void initialize_switch() {
  pinMode(BUTTON1, INPUT_PULLUP);
  pinMode(BUTTON2, INPUT_PULLUP);
  // Wherever the switch is at startup is its starting position, so a switch
  // left on doesn't start a run when the board powers up
  position = last_read = read_position();
}
