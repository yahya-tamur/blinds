#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "board.h"
#include "tmc2209.h"
#include "tests.h"

// STEP pulses come from an LEDC PWM channel: one pulse per period, so frequency = microsteps per second.
#define STEP_MODE  LEDC_LOW_SPEED_MODE
#define STEP_TIMER LEDC_TIMER_0
#define STEP_CH    LEDC_CHANNEL_0

void step_init(void)
{
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_MOT_EN) | (1ULL << PIN_DIR),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&out));
    gpio_set_level(PIN_MOT_EN, 1);                 // driver off until set up
    ledc_timer_config_t t = {
        .speed_mode = STEP_MODE, .duty_resolution = LEDC_TIMER_10_BIT,   // 10 bits reach down to ~76 Hz (8 bits stop at ~305 Hz)
        .timer_num = STEP_TIMER, .freq_hz = 1000, .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    ledc_channel_config_t c = {
        .gpio_num = PIN_STEP, .speed_mode = STEP_MODE, .channel = STEP_CH,
        .timer_sel = STEP_TIMER, .duty = 0, .hpoint = 0,   // duty 0 = no pulses yet
    };
    ESP_ERROR_CHECK(ledc_channel_config(&c));
}

// rate in microsteps per second; 0 stops the pulses
void step_rate(uint32_t rate)
{
    if (rate) ledc_set_freq(STEP_MODE, STEP_TIMER, rate);
    ledc_set_duty(STEP_MODE, STEP_CH, rate ? 512 : 0);
    ledc_update_duty(STEP_MODE, STEP_CH);
}

static void ramp(uint32_t from, uint32_t to, int ms)
{
    const int n = 50;
    for (int i = 1; i <= n; i++) {
        step_rate(from + (uint32_t)((int64_t)((int64_t)to - from) * i / n));
        vTaskDelay(pdMS_TO_TICKS(ms / n));
    }
}

void test_motor(void)
{
    step_init();
    tmc_uart_init();
    if (!tmc_setup(16, MOTOR_IRMS)) return;        // 16 microsteps, current from board.h
    gpio_set_level(PIN_MOT_EN, 0);                 // enable the driver
    const uint32_t fast = 3200;                    // 3200 microsteps/s = 1 rev/s on a 200-step motor
    printf("Motor test: one turn each way, repeating\n");
    for (int dir = 0;; dir ^= 1) {
        gpio_set_level(PIN_DIR, dir);
        printf("Direction %d\n", dir);
        ramp(200, fast, 500);
        vTaskDelay(pdMS_TO_TICKS(500));
        ramp(fast, 200, 500);
        step_rate(0);
        tmc_print_status();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
