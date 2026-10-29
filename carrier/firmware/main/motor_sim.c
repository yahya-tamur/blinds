// Simulated motor: a run lasts SIM_RUN_MS and moves nothing. The step engine replaces this file
// once the motor settings (current, chopper mode, planned stops) have been tested.
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "app_config.h"
#include "motor.h"

static const char *TAG = "motor";
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static int dir;
static int64_t end_us;

void motor_init(void)
{
    ESP_LOGW(TAG, "Simulated motor: runs last %d ms and nothing moves", SIM_RUN_MS);
}

void motor_run(int d)
{
    taskENTER_CRITICAL(&lock);
    dir = d;
    end_us = esp_timer_get_time() + SIM_RUN_MS * 1000LL;
    taskEXIT_CRITICAL(&lock);
    ESP_LOGI(TAG, "Run %s started", d > 0 ? "up" : "down");
}

void motor_cancel(void)
{
    taskENTER_CRITICAL(&lock);
    end_us = 0;
    taskEXIT_CRITICAL(&lock);
}

uint32_t motor_remaining_ms(void)
{
    taskENTER_CRITICAL(&lock);
    int64_t left = end_us - esp_timer_get_time();
    if (left <= 0) dir = 0;
    taskEXIT_CRITICAL(&lock);
    return left > 0 ? (uint32_t)(left / 1000) : 0;
}

bool motor_running(void) { return motor_remaining_ms() > 0; }
int motor_dir(void) { motor_remaining_ms(); return dir; }
uint32_t motor_total_ms(void) { return SIM_RUN_MS; }
