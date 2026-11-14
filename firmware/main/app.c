// Startup, and the control task: the one place that decides what the motor does. Every input posts
// commands to its queue; up or down starts a run unless one is going, cancel ends it. Once a run is
// ending (slowing down to its stop, or the pause and back-off) every command is ignored, and so is
// every command while a firmware update is being written. The task watchdog restarts the board if the
// control task ever stops running for CONFIG_ESP_TASK_WDT_TIMEOUT_S; it also logs the health stats.
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"
#include "alarms.h"
#include "sun.h"
#include "app.h"
#include "clock.h"
#include "events.h"
#include "inputs.h"
#include "logbuf.h"
#include "motor.h"
#include "ota.h"
#include "stats.h"
#include "travel.h"
#include "web.h"
#include "wifi.h"

static const char *TAG = "control";
static QueueHandle_t queue;

#define STATS_FIRST_S  30     // first health log after startup (Wi-Fi has connected by then)
#define STATS_EVERY_S  300

void event_post(command_t cmd, source_t source) { event_post_arg(cmd, source, 0); }

void event_post_arg(command_t cmd, source_t source, int arg)
{
    event_t ev = { cmd, source, arg };
    if (!queue) return;            // app_start not run (the Wi-Fi bring-up test serves the same page)
    if (xQueueSend(queue, &ev, 0) != pdTRUE) ESP_LOGW(TAG, "Command queue full, dropped a command");
}

static const char *source_name(source_t s)
{
    return s == SRC_SWITCH ? "switch" : s == SRC_REMOTE ? "remote" : s == SRC_WEB ? "web page" : "alarm";
}

static void handle(event_t ev)
{
    if (ota_busy()) {
        ESP_LOGW(TAG, "Firmware update in progress, ignored a command from %s", source_name(ev.source));
        return;
    }
    if (ev.cmd == CMD_SET_STOP || ev.cmd == CMD_CLEAR_STOP) {
        if (motor_running()) return;
        if (ev.cmd == CMD_SET_STOP) travel_set_stop(ev.arg);
        else travel_clear_stop(ev.arg);
        return;
    }
    if (ev.cmd == CMD_JOG_UP || ev.cmd == CMD_JOG_DOWN) {
        if (!motor_running()) travel_jog(ev.cmd == CMD_JOG_UP ? 1 : -1, ev.arg);
        return;
    }
    if (motor_finishing()) {
        ESP_LOGD(TAG, "Run ending, ignored a command from %s", source_name(ev.source));
        return;
    }
    if (ev.cmd == CMD_CANCEL) {
        if (motor_running()) {
            motor_cancel();
            ESP_LOGI(TAG, "Cancelled by %s", source_name(ev.source));
        }
        return;
    }
    if (motor_running()) {
        // The switch the other way stops the run; the same way, or up or down from anything else
        // (as in the Arduino version), is ignored
        if (ev.source == SRC_SWITCH && (ev.cmd == CMD_UP ? 1 : -1) != motor_dir()) {
            motor_cancel();
            ESP_LOGI(TAG, "Stopped by the switch");
        }
        return;
    }
    ESP_LOGI(TAG, "%s from %s", ev.cmd == CMD_UP ? "Up" : "Down", source_name(ev.source));
    travel_run(ev.cmd == CMD_UP ? 1 : -1);
}

static void control_task(void *arg)
{
    switch_init();
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));     // a hang here (more than 5 s) restarts the board
    int64_t next_alarm_check = 0;
    int64_t next_stats = xTaskGetTickCount() + pdMS_TO_TICKS(STATS_FIRST_S * 1000);
    while (1) {
        esp_task_wdt_reset();
        event_t ev;
        if (xQueueReceive(queue, &ev, pdMS_TO_TICKS(10)) == pdTRUE) handle(ev);
        switch_poll();
        travel_poll();
        int64_t now = xTaskGetTickCount();
        if (now >= next_stats) {
            next_stats = now + pdMS_TO_TICKS(STATS_EVERY_S * 1000);
            health_log();
        }
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

    health_init();
    ota_init();
    clock_init();
    sun_init();
    alarms_init();
    motor_init();
    motor_report_breadcrumb();
    travel_init();
    queue = xQueueCreate(8, sizeof(event_t));
    xTaskCreate(control_task, "control", 4096, NULL, 6, NULL);
    remote_start();
    wifi_start();      // returns at once; connects (and keeps reconnecting) in the background
    web_start();
}
