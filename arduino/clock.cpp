#include "log.h"
#include "RTClib.h"

#include "clock.h"

#include <Arduino.h> 

RTC_DS3231 rtc;

void initialize_clock() {
  // Initialize the I2C communication and the RTC module
  if (!rtc.begin()) {
    Log.println("Couldn't find RTC.");
  }
}

void set_clock(unsigned long epoch) {
  rtc.adjust(DateTime(epoch));
}

// US Pacific time. The RTC holds UTC.
const int STANDARD_OFFSET = -8;  // PST
const int DAYLIGHT_OFFSET = -7;  // PDT

// Day of the month of the first Sunday in the given month.
static int first_sunday(int year, int month) {
  int dow = DateTime(year, month, 1).dayOfTheWeek();  // 0 = Sunday
  return 1 + (7 - dow) % 7;
}

// US rule: DST runs from 2:00 PST on the second Sunday of March
// to 2:00 PDT on the first Sunday of November.
static bool is_dst(const DateTime& utc) {
  int year = utc.year();
  DateTime start(year, 3, first_sunday(year, 3) + 7, 2 - STANDARD_OFFSET, 0, 0);
  DateTime end(year, 11, first_sunday(year, 11), 2 - DAYLIGHT_OFFSET, 0, 0);
  return utc.unixtime() >= start.unixtime() && utc.unixtime() < end.unixtime();
}

static DateTime local_now() {
  DateTime utc = rtc.now();
  int offset = is_dst(utc) ? DAYLIGHT_OFFSET : STANDARD_OFFSET;
  return utc + TimeSpan(0, offset, 0, 0);
}

int get_minute() {
  DateTime localTime = local_now();
  return localTime.hour()*60 + localTime.minute();
}

long get_second() {
  DateTime localTime = local_now();
  return (localTime.hour()*60L + localTime.minute())*60 + localTime.second();
}