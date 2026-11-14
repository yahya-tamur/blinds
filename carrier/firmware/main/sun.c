#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "sun.h"

static const char *TAG = "sun";
static const char *names[SUN_EVENTS] = { "dawn", "sunrise", "sunset", "dusk" };
static bool has_location;
static int16_t lat10, lon10;   // tenths of a degree, north and east positive

void sun_init(void)
{
    nvs_handle_t h;
    int32_t v;
    if (nvs_open("blinds", NVS_READONLY, &h) != ESP_OK) return;
    if (nvs_get_i32(h, "location", &v) == ESP_OK) {
        lat10 = (int16_t)(v >> 16);
        lon10 = (int16_t)(v & 0xFFFF);
        has_location = true;
        ESP_LOGI(TAG, "Location %.1f, %.1f", lat10 / 10.0, lon10 / 10.0);
    }
    nvs_close(h);
}

bool sun_location(float *lat, float *lon)
{
    if (!has_location) return false;
    *lat = lat10 / 10.0f;
    *lon = lon10 / 10.0f;
    return true;
}

bool sun_set_location(float lat, float lon)
{
    if (!(lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180)) return false;   // also catches NaN
    lat10 = (int16_t)lroundf(lat * 10);
    lon10 = (int16_t)lroundf(lon * 10);
    has_location = true;
    nvs_handle_t h;
    if (nvs_open("blinds", NVS_READWRITE, &h) == ESP_OK) {
        int32_t v = (int32_t)((uint32_t)(uint16_t)lat10 << 16 | (uint16_t)lon10);
        if (nvs_set_i32(h, "location", v) != ESP_OK || nvs_commit(h) != ESP_OK) ESP_LOGE(TAG, "Couldn't save the location");
        nvs_close(h);
    }
    ESP_LOGI(TAG, "Location set to %.1f, %.1f", lat10 / 10.0, lon10 / 10.0);
    return true;
}

void sun_clear_location(void)
{
    has_location = false;
    nvs_handle_t h;
    if (nvs_open("blinds", NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, "location");
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "Location cleared");
}

const char *sun_name(int event) { return event >= 0 && event < SUN_EVENTS ? names[event] : "?"; }

int sun_from_name(const char *name)
{
    for (int i = 0; i < SUN_EVENTS; i++) {
        if (!strcmp(name, names[i])) return i;
    }
    return -1;
}

// Days since 1970-01-01 for a date in the Gregorian calendar
static long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

#define RAD (M_PI / 180)

bool sun_time(time_t day, int event, time_t *out)
{
    if (!has_location || event < 0 || event >= SUN_EVENTS) return false;
    struct tm lt;
    localtime_r(&day, &lt);
    double lat = lat10 / 10.0, lon = lon10 / 10.0;
    // Whole days from J2000 (2000-01-01 12:00 UTC) to noon UTC on this local date, then to local solar noon
    double n = days_from_civil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday) - 10957;
    double j = n - lon / 360;
    double m = fmod(357.5291 + 0.98560028 * j, 360);                                     // mean anomaly
    double c = 1.9148 * sin(m * RAD) + 0.0200 * sin(2 * m * RAD) + 0.0003 * sin(3 * m * RAD);
    double l = fmod(m + c + 180 + 102.9372, 360);                                        // ecliptic longitude
    double transit = j + 0.0053 * sin(m * RAD) - 0.0069 * sin(2 * l * RAD);              // days from J2000
    double sin_d = sin(l * RAD) * sin(23.4397 * RAD);
    double cos_d = sqrt(1 - sin_d * sin_d);
    // Sunrise and sunset: the top of the sun on the horizon, with refraction; dawn and dusk: 6 degrees below
    double h0 = event == SUN_DAWN || event == SUN_DUSK ? -6.0 : -0.833;
    double cos_w = (sin(h0 * RAD) - sin(lat * RAD) * sin_d) / (cos(lat * RAD) * cos_d);
    if (cos_w < -1 || cos_w > 1) return false;   // the sun stays above or below that height all day
    double w = acos(cos_w) / RAD / 360;          // half the time between rise and set, in days
    double t = event == SUN_DAWN || event == SUN_SUNRISE ? transit - w : transit + w;
    *out = (time_t)llround((t + 10957.5) * 86400);
    return true;
}
