#pragma once
#include <stdint.h>

void test_switches(void);
void test_ir(void);
void test_motor(void);
void test_stallguard(void);
void test_rtc(void);
void test_spreadcycle(void);

// STEP pulse helpers (test_motor.c)
void step_init(void);
void step_rate(uint32_t microsteps_per_s);
