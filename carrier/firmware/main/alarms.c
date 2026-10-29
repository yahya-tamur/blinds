#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs.h"
#include "alarms.h"
#include "clock.h"

static const char *TAG = "alarms";
static alarm_t alarms[MAX_ALARMS];
static int count;
static int last_minute = -1;
static SemaphoreHandle_t lock;   // the web server and the control task both use the list

static bool valid(int minute, int dir)
{
    return minute >= 0 && minute < 24 * 60 && (dir == 1 || dir == -1);
}

static void save(void)
{
    nvs_handle_t h;
    if (nvs_open("blinds", NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_blob(h, "alarms", alarms, count * sizeof(alarm_t)) != ESP_OK || nvs_commit(h) != ESP_OK) {
        ESP_LOGE(TAG, "Couldn't save the alarms");
    }
    nvs_close(h);
}

void alarms_init(void)
{
    lock = xSemaphoreCreateMutex();
    nvs_handle_t h;
    alarm_t stored[MAX_ALARMS];
    size_t size = sizeof stored;
    if (nvs_open("blinds", NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_blob(h, "alarms", stored, &size) == ESP_OK) {
            for (size_t i = 0; i < size / sizeof(alarm_t); i++) {
                if (valid(stored[i].minute, stored[i].dir)) alarms[count++] = stored[i];
            }
        }
        nvs_close(h);
    }
    ESP_LOGI(TAG, "%d alarm%s loaded", count, count == 1 ? "" : "s");
}

int alarms_list(alarm_t out[MAX_ALARMS])
{
    xSemaphoreTake(lock, portMAX_DELAY);
    memcpy(out, alarms, count * sizeof(alarm_t));
    int n = count;
    xSemaphoreGive(lock);
    return n;
}

bool alarms_add(int minute, int dir)
{
    if (!valid(minute, dir)) return false;
    xSemaphoreTake(lock, portMAX_DELAY);
    bool ok = count < MAX_ALARMS;
    if (ok) {
        int i = count;
        while (i > 0 && alarms[i - 1].minute > minute) {   // keep the list sorted by time
            alarms[i] = alarms[i - 1];
            i--;
        }
        alarms[i] = (alarm_t){ minute, dir };
        count++;
        save();
    }
    xSemaphoreGive(lock);
    if (ok) ESP_LOGI(TAG, "Added %02d:%02d %s", minute / 60, minute % 60, dir > 0 ? "up" : "down");
    return ok;
}

// By value rather than by position, so a page that's out of date can't remove the wrong alarm
bool alarms_remove(int minute, int dir)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    int i = 0;
    while (i < count && !(alarms[i].minute == minute && alarms[i].dir == dir)) i++;
    bool found = i < count;
    if (found) {
        memmove(&alarms[i], &alarms[i + 1], (count - i - 1) * sizeof(alarm_t));
        count--;
        save();
    }
    xSemaphoreGive(lock);
    if (found) ESP_LOGI(TAG, "Removed %02d:%02d %s", minute / 60, minute % 60, dir > 0 ? "up" : "down");
    return found;
}

int alarms_check(void)
{
    clock_poll();
    if (!clock_valid()) return 0;
    struct tm lt;
    clock_local(&lt);
    int now = lt.tm_hour * 60 + lt.tm_min;
    if (now == last_minute) return 0;
    last_minute = now;
    int dir = 0;
    xSemaphoreTake(lock, portMAX_DELAY);
    for (int i = 0; i < count && !dir; i++) {
        if (alarms[i].minute == now) dir = alarms[i].dir;
    }
    xSemaphoreGive(lock);
    if (dir) ESP_LOGI(TAG, "Alarm: %s", dir > 0 ? "up" : "down");
    return dir;
}
