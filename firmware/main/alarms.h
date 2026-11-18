// Alarms: run the blinds up or down at a set time. Three kinds, saved in NVS:
//   recurring  every day at a local hour and minute
//   once       at a moment in the next 24 hours (Unix time, a whole minute), then removed; also removed if missed
//   sun        every day at dawn, sunrise, sunset or dusk (sun.c) plus an offset in minutes
// At most one of each: one recurring alarm per minute of the day, one one-time alarm per moment, one alarm
// per sun event. Adding one that matches an existing alarm replaces it. If several fall on the same minute,
// one runs: one-time before recurring before sun (dawn, sunrise, sunset, dusk).
// A recurring or sun alarm can skip its next time once (skip_at); then it goes on as before.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "app_config.h"

typedef enum { ALARM_RECURRING, ALARM_ONCE, ALARM_SUN } alarm_kind_t;

#define SUN_OFFSET_MAX 180   // minutes either side of the sun event

typedef struct {
    int8_t kind;      // alarm_kind_t
    int8_t dir;       // 1 up, -1 down
    int8_t event;     // ALARM_SUN: sun_event_t
    int16_t minute;   // ALARM_RECURRING: minutes since local midnight, 0..1439; ALARM_SUN: offset, -180..180
    int64_t at;       // ALARM_ONCE: Unix time
    int64_t skip_at;  // recurring and sun: the one time it skips (Unix time), or 0
} alarm_t;

void alarms_init(void);
int alarms_list(alarm_t out[MAX_ALARMS]);   // copies the alarms, returns how many
bool alarms_add(const alarm_t *a);          // false if the list is full or the values are invalid
bool alarms_remove(const alarm_t *a);       // false if there's no such alarm (all fields must match)
// Skip (on) the alarm's next time once, or don't (off). Matched like alarms_remove; false if there's no such
// alarm, it is one-time, or its next time isn't known
bool alarms_skip(const alarm_t *a, bool on);
// When the alarm next goes off after `now`; false if it can't be known (a sun alarm without a location).
// A skipped time counts: alarms_next_run leaves it out.
bool alarms_next(const alarm_t *a, time_t now, time_t *out);
bool alarms_next_run(const alarm_t *a, time_t now, time_t *out);
const char *alarms_kind_name(int kind);     // "recurring", "once", "sun"
// Call once a second. Returns 1 or -1 once when an alarm's minute arrives, else 0.
int alarms_check(void);
