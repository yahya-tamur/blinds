#ifndef SWITCH_H
#define SWITCH_H

#define BUTTON1 5
#define BUTTON2 6

// Moving the switch from off to up or down acts like pressing up or down: a
// whole run, even if the switch is left on. Moving it back to off cancels.
int poll_switch();

void initialize_switch();

#endif