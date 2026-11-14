// Where the blinds are, and where runs stop. The position is the Hall count (12 counts per turn of the
// sprocket, 30 degrees each), increasing upward; the PCNT counter also follows the blinds when they are moved
// by hand while the board is on. It is saved to NVS, so it survives a restart, but not a move made while the
// board was off (then set the stops again, or re-home once that exists).
//
// Up runs to the top stop and Down to the bottom stop, stopping when the Hall count gets there; without its
// stop a run doesn't start (jogs still work, for setting the stops). Every move stops if the Hall count stops changing
// (a stall). The plan for learned speeds, end-stop homing and retries is in the "Learned motor behaviour"
// section of the Next Steps doc.
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { STOP_TOP, STOP_BOTTOM };

void travel_init(void);
void travel_poll(void);                  // call every loop of the control task (10 ms)
int travel_pos(void);
bool travel_last_driver(uint32_t *status);   // DRV_STATUS read 1 s into the last move (moves over 1 s)
bool travel_get_stop(int which, int *pos);
bool travel_set_stop(int which);         // the current position becomes this stop; false if on the wrong side
void travel_clear_stop(int which);
void travel_run(int dir);                // Up or Down: to the stop; nothing if that stop isn't set
float travel_speed(int dir);             // run speed, rev/s (saved per direction)
void travel_set_speed(int dir, float rps);   // 0.1 .. MOTOR_MAX_SPEED, in steps of 0.1
void travel_jog(int dir, int counts);    // a short move without back-off, for setting the stops
bool travel_is_jog(void);                 // the current move is a jog
