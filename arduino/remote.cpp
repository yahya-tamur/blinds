#include "remote.h"
#include "poll.h"

#include <IRremote.h>
#include <Arduino.h>

// Decodes the next IR frame, ignoring repeat frames sent while a button is held
// and noise that doesn't match any protocol. Returns false if there was no new press.
static bool next_press(uint16_t* command) {
  if (!IrReceiver.decode()) {
    return false;
  }
  bool fresh = !(IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT)
               && IrReceiver.decodedIRData.protocol != UNKNOWN;
  *command = IrReceiver.decodedIRData.command;
  IrReceiver.resume();
  return fresh;
}

int poll_remote() {
  uint16_t p;
  if (next_press(&p)) {
    if (p == 0x7) {
      return 1;
    }
    if (p == 0x15) {
      return -1;
    }
    if (p == 0x9) {
      return CMD_CANCEL;
    }
  }
  return 0;
}


void initialize_remote() {
  IrReceiver.begin(REMOTE, true);
}
