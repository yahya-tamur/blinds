// The motor as the rest of the application sees it: start a run, cancel it, ask what it is doing.
// motor_steps.c implements it.
#pragma once
#include <stdbool.h>
#include <stdint.h>

void motor_init(void);
void motor_run(int dir);          // dir 1 = up, -1 = down; a whole run of the fixed MOTOR_RUN_REVS
void motor_move(int dir, uint32_t steps, bool backoff, float speed);   // any length (back-off: as a run does), rev/s
uint32_t motor_backoff_steps(void);   // steps in the back-off at the end of a run
uint32_t motor_default_steps(void);   // steps in a fixed run
bool motor_main_move(void);       // in the main part of a move (not pausing or backing off)
uint32_t motor_steps_run(void);   // steps done in the main part of the current move
bool motor_reapply(void);         // send the driver its settings again (after it reset mid-move)
void motor_report_breadcrumb(void);   // logs where the last move was when the board restarted (call at start)
const char *motor_breadcrumb(void);   // the same, as text, for /api/status
void motor_set(float irms, int spreadcycle);   // run current (A) and mode (1/0, -1 keep), saved
float motor_speed(void);
float motor_irms(void);
bool motor_spreadcycle(void);
void motor_cancel(void);          // ends the run early
// The motor turns the other way from MOTOR_UP_DIR (it was rewired): travel.c notices from the Hall count and
// sets this; saved in NVS ("mot_rev") when save is true. Takes effect from the next move.
void motor_set_reversed(bool reversed, bool save);
bool motor_reversed(void);
bool motor_running(void);
bool motor_finishing(void);       // slowing down to its stop, pausing or backing off: the run is ending anyway
int motor_dir(void);              // 1, -1, or 0 at rest
uint32_t motor_total_ms(void);    // expected length of the current run
uint32_t motor_remaining_ms(void);
