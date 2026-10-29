// Daily alarms: at a given local minute, run the blinds up or down. Kept sorted by time and saved in NVS.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"

typedef struct {
    int16_t minute;   // minutes since local midnight, 0..1439
    int8_t dir;       // 1 up, -1 down
} alarm_t;

void alarms_init(void);
int alarms_list(alarm_t out[MAX_ALARMS]);   // copies the alarms, returns how many
bool alarms_add(int minute, int dir);       // false if the list is full or the values are invalid
bool alarms_remove(int minute, int dir);    // false if there's no such alarm
// Call once a second. Returns 1 or -1 once when an alarm's minute arrives, else 0.
int alarms_check(void);
