#pragma once
#include <stdint.h>
#include "driver/pulse_cnt.h"

void test_bringup(void);      // a newly built board: every part in turn, then a pass/fail list

// STEP pulse helpers (tests/motor.c); step_init leaves the relay on
void step_init(void);
void step_rate(uint32_t microsteps_per_s);

// Hall sensors (tests/hall.c): with 6 alternating magnets, 12 counts per turn of the magnet gear
#define HALL_COUNTS_PER_REV 12
pcnt_unit_handle_t hall_counter_init(void);

// NEC remote receiver (tests/ir.c): ir_receive returns 1 for a good NEC frame (printed), 0 for another
// frame, -1 for nothing within timeout_ms (-1 = wait for ever)
void ir_init(void);
int ir_receive(int timeout_ms);

// Local, setup-specific tests in firmware/local/tests/ (git-ignored), declared there
#if __has_include("../../local/tests/local_tests.h")
#include "../../local/tests/local_tests.h"
#endif
