#include "alarms.h"
#include "clock.h"

#include <Arduino.h>
#include <EEPROM.h>

// EEPROM layout: [MAGIC][count][Alarm 0][Alarm 1]...
static const uint8_t MAGIC = 0xA5;
static const int ALARMS_ADDR = 2;

static Alarm alarms[MAX_ALARMS];
static int num_alarms = 0;
static int last_minute = -1;

static bool valid(int minute, int dir) {
  return minute >= 0 && minute < 24 * 60 && (dir == ALARM_UP || dir == ALARM_DOWN);
}

static void save_alarms() {
  EEPROM.update(0, MAGIC);
  EEPROM.update(1, num_alarms);
  for (int i = 0; i < num_alarms; i++) {
    EEPROM.put(ALARMS_ADDR + i * sizeof(Alarm), alarms[i]);
  }
}

void initialize_alarms() {
  num_alarms = 0;
  if (EEPROM.read(0) != MAGIC) {
    return;
  }
  int stored = EEPROM.read(1);
  for (int i = 0; i < stored && i < MAX_ALARMS; i++) {
    Alarm a;
    EEPROM.get(ALARMS_ADDR + i * sizeof(Alarm), a);
    if (valid(a.minute, a.dir)) {
      alarms[num_alarms++] = a;
    }
  }
}

int alarm_count() {
  return num_alarms;
}

Alarm get_alarm(int i) {
  return alarms[i];
}

bool add_alarm(int minute, int dir) {
  if (num_alarms >= MAX_ALARMS || !valid(minute, dir)) {
    return false;
  }
  // keep the list sorted by time
  int i = num_alarms;
  while (i > 0 && alarms[i - 1].minute > minute) {
    alarms[i] = alarms[i - 1];
    i--;
  }
  alarms[i].minute = minute;
  alarms[i].dir = dir;
  num_alarms++;
  save_alarms();
  return true;
}

void remove_alarm(int i) {
  if (i < 0 || i >= num_alarms) {
    return;
  }
  for (; i < num_alarms - 1; i++) {
    alarms[i] = alarms[i + 1];
  }
  num_alarms--;
  save_alarms();
}

int check_alarms() {
  int now = get_minute();
  if (now == last_minute) {
    return 0;
  }
  last_minute = now;
  for (int i = 0; i < num_alarms; i++) {
    if (alarms[i].minute == now) {
      return alarms[i].dir;
    }
  }
  return 0;
}
