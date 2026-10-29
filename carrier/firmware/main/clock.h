// Time of day. The DS3231 keeps UTC through power cuts; the system clock is set from it at boot and
// from the internet (SNTP) whenever Wi-Fi is up. Local time comes from the APP_TZ rule.
#pragma once
#include <stdbool.h>
#include <time.h>

void clock_init(void);
bool clock_valid(void);             // false until the DS3231 or the internet has given a real time
void clock_local(struct tm *out);   // local time now
void clock_set_utc(time_t utc);     // from SNTP: sets the system clock and the DS3231
void clock_poll(void);              // once a second: re-reads the DS3231 hourly while there's no internet time
