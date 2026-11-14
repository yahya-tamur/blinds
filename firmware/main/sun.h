// Sun times (dawn, sunrise, sunset, dusk) from the saved location, worked out on the board with the
// sunrise equation (NOAA's simplified form, good to about a minute). Dawn and dusk are civil twilight:
// the sun 6 degrees below the horizon. The location is set from the web page, one decimal place.
#pragma once
#include <stdbool.h>
#include <time.h>

typedef enum { SUN_DAWN, SUN_SUNRISE, SUN_SUNSET, SUN_DUSK, SUN_EVENTS } sun_event_t;

void sun_init(void);                          // loads the location from NVS
bool sun_location(float *lat, float *lon);    // false if no location is set
bool sun_set_location(float lat, float lon);  // rounded to 0.1 degree and saved; false if out of range
void sun_clear_location(void);
const char *sun_name(int event);              // "dawn", "sunrise", "sunset", "dusk"
int sun_from_name(const char *name);          // -1 if unknown
// When the event happens on the local calendar date that contains `day` (any time that day).
// False without a location, or if the sun doesn't reach that height that day (polar summer or winter).
bool sun_time(time_t day, int event, time_t *out);
