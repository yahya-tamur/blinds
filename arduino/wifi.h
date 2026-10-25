#ifndef WIFI_H
#define WIFI_H

int poll_wifi();

void initialize_wifi();

// Tells the /moving page what the motor is doing: dir 1 (up) / -1 (down) for ms, or 0 when stopped.
void set_motor_state(int dir, unsigned long ms);

#endif
