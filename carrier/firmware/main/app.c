// Startup, and the control task: the one place that decides what the motor does. Every input posts
// commands to its queue; up or down starts a run unless one is going, cancel ends it.
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "alarms.h"
#include "app.h"
#include "clock.h"
#include "events.h"
#include "inputs.h"
#include "logbuf.h"
#include "motor.h"
#include "web.h"
#include "wifi.h"

static const char *TAG = "control";
static QueueHandle_t queue;

void event_post(command_t cmd, source_t source)
{
    event_t ev = { cmd, source };
    if (xQueueSend(queue, &ev, 0) != pdTRUE) ESP_LOGW(TAG, "Command queue full, dropped a command");
}

static const char *source_name(source_t s)
{
    return s == SRC_SWITCH ? "switch" : s == SRC_REMOTE ? "remote" : s == SRC_WEB ? "web page" : "alarm";
}

static void handle(event_t ev)
{
    if (ev.cmd == CMD_CANCEL) {
        if (motor_running()) {
            motor_cancel();
            ESP_LOGI(TAG, "Cancelled by %s", source_name(ev.source));
        }
        return;
    }
    // Up or down while a run is going is ignored, as in the Arduino version
    if (motor_running()) return;
    ESP_LOGI(TAG, "%s from %s", ev.cmd == CMD_UP ? "Up" : "Down", source_name(ev.source));
    motor_run(ev.cmd == CMD_UP ? 1 : -1);
}

static void control_task(void *arg)
{
    switch_init();
    int64_t next_alarm_check = 0;
    while (1) {
        event_t ev;
        if (xQueueReceive(queue, &ev, pdMS_TO_TICKS(10)) == pdTRUE) handle(ev);
        switch_poll();
        int64_t now = xTaskGetTickCount();
        if (now >= next_alarm_check) {
            next_alarm_check = now + pdMS_TO_TICKS(1000);
            int dir = alarms_check();
            if (dir) event_post(dir > 0 ? CMD_UP : CMD_DOWN, SRC_ALARM);
        }
    }
}

void app_start(void)
{
    logbuf_init();
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    clock_init();
    alarms_init();
    motor_init();
    queue = xQueueCreate(8, sizeof(event_t));
    xTaskCreate(control_task, "control", 4096, NULL, 6, NULL);
    remote_start();
    wifi_start();      // returns at once; connects (and keeps reconnecting) in the background
    web_start();
}
