// The motor as the rest of the application sees it: start a run, cancel it, ask what it is doing.
// For now motor_sim.c stands in for the real step engine, so nothing moves.
#pragma once
#include <stdbool.h>
#include <stdint.h>

void motor_init(void);
void motor_run(int dir);          // dir 1 = up, -1 = down; a whole run
void motor_cancel(void);          // ends the run early
bool motor_running(void);
int motor_dir(void);              // 1, -1, or 0 at rest
uint32_t motor_total_ms(void);    // expected length of the current run
uint32_t motor_remaining_ms(void);
