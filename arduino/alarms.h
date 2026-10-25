#ifndef ALARMS_H
#define ALARMS_H

#include <stdint.h>

#define MAX_ALARMS 20

#define ALARM_UP 1
#define ALARM_DOWN -1

struct Alarm {
  int16_t minute;  // minutes since local midnight, 0..1439
  int8_t dir;      // ALARM_UP or ALARM_DOWN
};

void initialize_alarms();

int alarm_count();
Alarm get_alarm(int i);

bool add_alarm(int minute, int dir);
void remove_alarm(int i);

// Returns ALARM_UP / ALARM_DOWN once when an alarm's minute is reached, else 0.
int check_alarms();

#endif
