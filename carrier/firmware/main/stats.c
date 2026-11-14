#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "stats.h"
#include "wifi.h"

static const char *TAG = "stats";
static char version[80];   // version (up to 32) + date + time

static const char *reset_name(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:   return "power on";
    case ESP_RST_EXT:       return "reset pin";
    case ESP_RST_SW:        return "restart (software or update)";
    case ESP_RST_PANIC:     return "crash (panic)";
    case ESP_RST_INT_WDT:   return "interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "task watchdog";
    case ESP_RST_WDT:       return "other watchdog";
    case ESP_RST_BROWNOUT:  return "brownout (supply dipped)";
    case ESP_RST_USB:       return "USB";
    case ESP_RST_JTAG:      return "JTAG";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    default:                return "unknown";
    }
}

void health_init(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    snprintf(version, sizeof version, "%s, %s %s", app->version, app->date, app->time);
    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "Firmware %s, running from %s", version, running ? running->label : "?");
    ESP_LOGI(TAG, "Last restart: %s", reset_name(esp_reset_reason()));
}

void health_get(health_t *s)
{
    s->heap_free = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    s->heap_min = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    s->heap_largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    wifi_ap_record_t ap;
    s->wifi = wifi_connected() && esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
    s->rssi = s->wifi ? ap.rssi : 0;
    s->uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
    s->reset = reset_name(esp_reset_reason());
    s->version = version;
}

void health_log(void)
{
    health_t s;
    health_get(&s);
    uint32_t h = s.uptime_s / 3600, m = s.uptime_s / 60 % 60;
    char wifi[24];
    if (s.wifi) snprintf(wifi, sizeof wifi, "Wi-Fi %d dBm", s.rssi);
    else strcpy(wifi, "Wi-Fi off");
    ESP_LOGI(TAG, "Up %luh%02lum, RAM %lu KB free (lowest %lu, largest block %lu), %s", (unsigned long)h,
             (unsigned long)m, (unsigned long)(s.heap_free / 1024), (unsigned long)(s.heap_min / 1024),
             (unsigned long)(s.heap_largest / 1024), wifi);

    // The three tasks closest to running out of stack (unused stack at its lowest, in bytes)
    TaskStatus_t tasks[24];
    UBaseType_t n = uxTaskGetSystemState(tasks, sizeof tasks / sizeof tasks[0], NULL);
    UBaseType_t kept = 0;          // leave out ESP-IDF's own small fixed-size tasks (IDLE, ipc)
    for (UBaseType_t i = 0; i < n; i++)
        if (strncmp(tasks[i].pcTaskName, "IDLE", 4) && strncmp(tasks[i].pcTaskName, "ipc", 3)) tasks[kept++] = tasks[i];
    n = kept;
    for (UBaseType_t i = 0; i < n; i++)      // sort by headroom, lowest first
        for (UBaseType_t j = i + 1; j < n; j++)
            if (tasks[j].usStackHighWaterMark < tasks[i].usStackHighWaterMark) {
                TaskStatus_t t = tasks[i];
                tasks[i] = tasks[j];
                tasks[j] = t;
            }
    char line[112] = "";
    for (UBaseType_t i = 0; i < n && i < 3; i++) {
        char part[48];
        snprintf(part, sizeof part, "%s%s %lu", i ? ", " : "", tasks[i].pcTaskName,
                 (unsigned long)tasks[i].usStackHighWaterMark);
        strlcat(line, part, sizeof line);
    }
    ESP_LOGI(TAG, "Stack headroom, tightest (bytes): %s", line);
}
