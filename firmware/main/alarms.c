#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs.h"
#include "alarms.h"
#include "clock.h"
#include "sun.h"

#define ONCE_MAX_AHEAD 86400   // one-time alarms are for the next 24 hours

static const char *TAG = "alarms";
static alarm_t alarms[MAX_ALARMS];
static int count;
static int64_t last_minute = -1;   // Unix minute last checked
static SemaphoreHandle_t lock;     // the web server and the control task both use the list

const char *alarms_kind_name(int kind)
{
    return kind == ALARM_RECURRING ? "recurring" : kind == ALARM_ONCE ? "once" : kind == ALARM_SUN ? "sun" : "?";
}

static bool valid(const alarm_t *a)
{
    if (a->dir != 1 && a->dir != -1) return false;
    switch (a->kind) {
    case ALARM_RECURRING: return a->minute >= 0 && a->minute < 24 * 60;
    case ALARM_ONCE: return a->at > 0 && a->at % 60 == 0;
    case ALARM_SUN: return a->event >= 0 && a->event < SUN_EVENTS && a->minute >= -SUN_OFFSET_MAX && a->minute <= SUN_OFFSET_MAX;
    default: return false;
    }
}

// Two alarms in the same slot can't both exist: the same minute of the day, the same moment, or the same sun event
static bool same_slot(const alarm_t *a, const alarm_t *b)
{
    if (a->kind != b->kind) return false;
    switch (a->kind) {
    case ALARM_RECURRING: return a->minute == b->minute;
    case ALARM_ONCE: return a->at == b->at;
    default: return a->event == b->event;
    }
}

// For the log: "recurring 07:30 up", "once 2026-11-15 07:30 up", "sun sunset+30 down"
static void describe(const alarm_t *a, char *buf, size_t size)
{
    const char *dir = a->dir > 0 ? "up" : "down";
    if (a->kind == ALARM_RECURRING) {
        snprintf(buf, size, "recurring %02d:%02d %s", a->minute / 60, a->minute % 60, dir);
    } else if (a->kind == ALARM_ONCE) {
        time_t t = (time_t)a->at;
        struct tm lt;
        localtime_r(&t, &lt);
        snprintf(buf, size, "once %04d-%02d-%02d %02d:%02d %s", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour,
                 lt.tm_min, dir);
    } else {
        snprintf(buf, size, "sun %s%+d %s", sun_name(a->event), a->minute, dir);
    }
}

static void save(void)
{
    nvs_handle_t h;
    if (nvs_open("blinds", NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_blob(h, "alarms3", alarms, count * sizeof(alarm_t)) != ESP_OK || nvs_commit(h) != ESP_OK) {
        ESP_LOGE(TAG, "Couldn't save the alarms");
    }
    nvs_close(h);
}

void alarms_init(void)
{
    lock = xSemaphoreCreateMutex();
    nvs_handle_t h;
    if (nvs_open("blinds", NVS_READWRITE, &h) != ESP_OK) return;
    alarm_t stored[MAX_ALARMS];
    size_t size = sizeof stored;
    // Before skipping, alarms were saved as "alarms2" without skip_at
    typedef struct { int8_t kind, dir, event; int16_t minute; int64_t at; } alarm2_t;
    alarm2_t old2[MAX_ALARMS];
    size_t size2 = sizeof old2;
    if (nvs_get_blob(h, "alarms3", stored, &size) == ESP_OK) {
        for (size_t i = 0; i < size / sizeof(alarm_t); i++) {
            if (valid(&stored[i])) alarms[count++] = stored[i];
        }
    } else if (nvs_get_blob(h, "alarms2", old2, &size2) == ESP_OK) {
        for (size_t i = 0; i < size2 / sizeof(alarm2_t); i++) {
            alarm_t a = { .kind = old2[i].kind, .dir = old2[i].dir, .event = old2[i].event, .minute = old2[i].minute,
                          .at = old2[i].at };
            if (valid(&a)) alarms[count++] = a;
        }
        nvs_close(h);
        save();
        if (nvs_open("blinds", NVS_READWRITE, &h) != ESP_OK) return;
        nvs_erase_key(h, "alarms2");
        nvs_commit(h);
    } else {
        // Firmware before one-time and sun alarms saved daily alarms only, as {minute, dir}: they become recurring
        struct { int16_t minute; int8_t dir; } old[MAX_ALARMS];
        size = sizeof old;
        if (nvs_get_blob(h, "alarms", old, &size) == ESP_OK) {
            for (size_t i = 0; i < size / sizeof old[0] && count < MAX_ALARMS; i++) {
                alarm_t a = { .kind = ALARM_RECURRING, .dir = old[i].dir, .minute = old[i].minute };
                if (valid(&a)) alarms[count++] = a;
            }
            nvs_close(h);
            save();
            if (nvs_open("blinds", NVS_READWRITE, &h) != ESP_OK) return;
            nvs_erase_key(h, "alarms");
            nvs_commit(h);
            ESP_LOGI(TAG, "Converted %d daily alarm%s to recurring alarms", count, count == 1 ? "" : "s");
        }
    }
    nvs_close(h);
    ESP_LOGI(TAG, "%d alarm%s loaded", count, count == 1 ? "" : "s");
}

int alarms_list(alarm_t out[MAX_ALARMS])
{
    if (!lock) return 0;          // alarms_init not run (the Wi-Fi bring-up test serves the same pages)
    xSemaphoreTake(lock, portMAX_DELAY);
    memcpy(out, alarms, count * sizeof(alarm_t));
    int n = count;
    xSemaphoreGive(lock);
    return n;
}

bool alarms_add(const alarm_t *a)
{
    if (!valid(a) || !lock) return false;
    if (a->kind == ALARM_ONCE) {
        // Needs the real time to tell whether the moment is still ahead
        time_t now = time(NULL);
        if (!clock_valid() || a->at <= now || a->at > now + ONCE_MAX_AHEAD) return false;
    }
    alarm_t copy = *a;
    if (copy.kind != ALARM_SUN) copy.event = 0;          // unused fields zero, so remove can match whole records
    if (copy.kind != ALARM_ONCE) copy.at = 0;
    if (copy.kind == ALARM_ONCE) copy.minute = 0;
    copy.skip_at = 0;
    xSemaphoreTake(lock, portMAX_DELAY);
    int i = 0;
    while (i < count && !same_slot(&alarms[i], &copy)) i++;
    bool replaced = i < count, ok = replaced || count < MAX_ALARMS;
    if (ok) {
        alarms[i] = copy;
        if (!replaced) count++;
        save();
    }
    xSemaphoreGive(lock);
    if (ok) {
        char text[48];
        describe(&copy, text, sizeof text);
        ESP_LOGI(TAG, "%s %s", replaced ? "Replaced with" : "Added", text);
    }
    return ok;
}

// The alarm matching a (by value, so an out-of-date page can't pick the wrong one), or -1
static int find(const alarm_t *a)
{
    int i = 0;
    while (i < count && !(same_slot(&alarms[i], a) && alarms[i].dir == a->dir &&
                          (a->kind != ALARM_SUN || alarms[i].minute == a->minute))) i++;
    return i < count ? i : -1;
}

static void remove_at(int i)
{
    memmove(&alarms[i], &alarms[i + 1], (count - i - 1) * sizeof(alarm_t));
    count--;
}

// By value rather than by position, so a page that's out of date can't remove the wrong alarm
bool alarms_remove(const alarm_t *a)
{
    if (!lock) return false;
    xSemaphoreTake(lock, portMAX_DELAY);
    int i = find(a);
    bool found = i >= 0;
    if (found) {
        remove_at(i);
        save();
    }
    xSemaphoreGive(lock);
    if (found) {
        char text[48];
        describe(a, text, sizeof text);
        ESP_LOGI(TAG, "Removed %s", text);
    }
    return found;
}

bool alarms_skip(const alarm_t *a, bool on)
{
    if (!lock || a->kind == ALARM_ONCE) return false;
    time_t now = time(NULL), next = 0;
    if (on && (!clock_valid() || !alarms_next(a, now, &next))) return false;
    xSemaphoreTake(lock, portMAX_DELAY);
    int i = find(a);
    if (i >= 0) {
        alarms[i].skip_at = on ? next : 0;
        save();
    }
    xSemaphoreGive(lock);
    if (i < 0) return false;
    char text[48];
    describe(a, text, sizeof text);
    if (on) {
        struct tm lt;
        localtime_r(&next, &lt);
        ESP_LOGI(TAG, "%s skips %04d-%02d-%02d %02d:%02d once", text, lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
                 lt.tm_hour, lt.tm_min);
    } else {
        ESP_LOGI(TAG, "%s no longer skips", text);
    }
    return true;
}

// Noon on the local date `days` days after the one containing `now`
static time_t local_noon(time_t now, int days)
{
    struct tm lt;
    localtime_r(&now, &lt);
    lt.tm_mday += days;
    lt.tm_hour = 12;
    lt.tm_min = lt.tm_sec = 0;
    lt.tm_isdst = -1;
    return mktime(&lt);
}

// A sun alarm's time on the local date `days` after today's: the event plus the offset
static bool sun_alarm_time(const alarm_t *a, time_t now, int days, time_t *out)
{
    time_t t;
    if (!sun_time(local_noon(now, days), a->event, &t)) return false;
    *out = t + a->minute * 60;
    return true;
}

bool alarms_next(const alarm_t *a, time_t now, time_t *out)
{
    if (a->kind == ALARM_ONCE) {
        *out = (time_t)a->at;
        return true;
    }
    if (a->kind == ALARM_RECURRING) {
        for (int days = 0; days <= 1; days++) {
            struct tm lt;
            localtime_r(&now, &lt);
            lt.tm_mday += days;
            lt.tm_hour = a->minute / 60;
            lt.tm_min = a->minute % 60;
            lt.tm_sec = 0;
            lt.tm_isdst = -1;
            time_t t = mktime(&lt);
            if (t > now) {
                *out = t;
                return true;
            }
        }
        return false;
    }
    // Sun: yesterday's event with a large offset can still be ahead, so start a day back
    for (int days = -1; days <= 2; days++) {
        time_t t;
        if (sun_alarm_time(a, now, days, &t) && t > now) {
            *out = t;
            return true;
        }
    }
    return false;
}

bool alarms_next_run(const alarm_t *a, time_t now, time_t *out)
{
    if (!alarms_next(a, now, out)) return false;
    if (a->skip_at && *out / 60 == a->skip_at / 60) return alarms_next(a, *out, out);
    return true;
}

int alarms_check(void)
{
    clock_poll();
    if (!clock_valid() || !lock) return 0;
    time_t now = time(NULL);
    int64_t minute = now / 60;
    if (minute == last_minute) return 0;
    last_minute = minute;
    time_t start = (time_t)(minute * 60);
    struct tm lt;
    localtime_r(&now, &lt);
    int local_minute = lt.tm_hour * 60 + lt.tm_min;

    int once = 0, recurring = 0, sun[SUN_EVENTS] = { 0 };
    bool changed = false;
    xSemaphoreTake(lock, portMAX_DELAY);
    for (int i = 0; i < count;) {
        const alarm_t *a = &alarms[i];
        if (a->kind == ALARM_ONCE && a->at <= start) {
            if (a->at == start) {
                once = a->dir;
            } else {
                char text[48];
                describe(a, text, sizeof text);
                ESP_LOGW(TAG, "Missed %s (the board was off or didn't know the time): removed", text);
            }
            remove_at(i);
            changed = true;
            continue;
        }
        alarm_t *m = &alarms[i];
        bool due = a->kind == ALARM_RECURRING && a->minute == local_minute;
        if (a->kind == ALARM_SUN) {
            for (int days = -1; days <= 1; days++) {
                time_t t;
                if (sun_alarm_time(a, now, days, &t) && t / 60 == minute) due = true;
            }
        }
        if (due && m->skip_at && m->skip_at / 60 == minute) {
            char text[48];
            describe(a, text, sizeof text);
            ESP_LOGI(TAG, "Skipped %s this once", text);
            m->skip_at = 0;
            changed = true;
        } else if (due) {
            if (a->kind == ALARM_RECURRING) recurring = a->dir;
            else sun[a->event] = a->dir;
        } else if (m->skip_at && m->skip_at / 60 < minute) {
            m->skip_at = 0;            // its time went by unseen (board off, or the sun time moved): forget it
            changed = true;
        }
        i++;
    }
    if (changed) save();
    xSemaphoreGive(lock);

    const char *which = once ? "one-time" : recurring ? "recurring" : NULL;
    int dir = once ? once : recurring;
    for (int e = 0; e < SUN_EVENTS && !dir; e++) {
        if (sun[e]) {
            dir = sun[e];
            which = sun_name(e);
        }
    }
    if (dir) ESP_LOGI(TAG, "Alarm (%s): %s", which, dir > 0 ? "up" : "down");
    return dir;
}
