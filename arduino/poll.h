#ifndef POLL_H
#define POLL_H

#define SOURCE_NONE 0
#define SOURCE_SWITCH 1
#define SOURCE_REMOTE 2
#define SOURCE_WIFI 3

// Returned by poll() for the remote's cancel button, the web page's Stop button
// and the switch going back to off
#define CMD_CANCEL 2

// Returns 1 (up), -1 (down), CMD_CANCEL or 0, and sets *source to where it came from.
int poll(int* source);

void initialize_all();

#endif
