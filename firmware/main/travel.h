// Where the blinds are, and where runs stop. The position is the Hall count (12 counts per turn of the
// sprocket, 30 degrees each), increasing upward, and 0 at the bottom end stop once the end stops are detected
// (before that, 0 is wherever the board first started counting). The top and bottom stops are numbers on that
// scale: detecting doesn't change them, so stops set after detecting stay right whatever happens meanwhile; the PCNT counter also follows the blinds when they are moved
// by hand while the board is on. It is saved to NVS once the blinds have been still for 5 s, so it survives a
// restart. A restart during a move (or within those 5 s) leaves it unknown: once the end stops have been
// detected, the next run first lowers into the bottom end stop at ENDSTOP_SPEED (re-homing), which sets it
// again. A move made by hand while the board was off can't be noticed: re-home from the page then.
//
// Up runs to the top stop and Down to the bottom stop, stopping when the Hall count gets there; without its
// stop a run doesn't start (jogs still work, for setting the stops). Every move stops if the Hall count stops changing
// (a stall). If the count runs against the command, the motor was rewired: the motor direction is swapped
// (saved) and the move starts again; a stall before any Hall edge first tries a short move with it swapped.
// The stops and end stops don't change, since the count follows the blinds, not the motor. The plan for learned speeds, end-stop homing and retries is in the "Learned motor behaviour"
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
void travel_cancel(void);                 // stop the current move (a command): no retry or direction probe after it
// End stops: the blinds' physical stops, found by running into them (a stall) at ENDSTOP_SPEED
void travel_detect_ends(void);            // up into the top end stop, then down into the bottom one; saved
void travel_home(void);                   // down into the bottom end stop: the position is known again
bool travel_get_ends(int *top, int *bottom);   // false until detected
bool travel_pos_known(void);              // false after a restart during a move, until re-homed
const char *travel_task(void);            // "detect_up", "detect_down", "home" while one runs, else ""
const char *travel_task_result(void);     // how the last detect or re-home ended (text for the page), or ""
