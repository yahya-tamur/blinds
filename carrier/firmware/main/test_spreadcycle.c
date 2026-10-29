#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "board.h"
#include "tmc2209.h"
#include "tests.h"

// SpreadCycle torque test: SW1 closed turns one way, SW2 the other, neither (or both) switches the driver off
// so the shaft turns freely. SpreadCycle gives more torque under load than StealthChop but is louder,
// and StallGuard does not work in it.
#define JOG_RATE   3200     // microsteps/s at full speed: 1 rev/s of the motor shaft at 16 microsteps
#define JOG_START  400      // microsteps/s to start from
#define JOG_ACCEL  8000     // microsteps/s per second: full speed after ~0.35 s

void test_spreadcycle(void)
{
    gpio_config_t sw = {
        .pin_bit_mask = (1ULL << PIN_SW1) | (1ULL << PIN_SW2),
        .mode = GPIO_MODE_INPUT,                 // 10k pull-ups are on the board
    };
    ESP_ERROR_CHECK(gpio_config(&sw));
    step_init();                                 // EN high: driver off
    tmc_uart_init();
    if (!tmc_setup(16, MOTOR_IRMS)) return;
    // Same GCONF as tmc_setup, plus en_SpreadCycle (bit 2)
    tmc_write(TMC_GCONF, (1u << 2) | (1u << 6) | (1u << 7) | (1u << 8));
    printf("SpreadCycle test: close SW1 or SW2 to turn the motor, open both to let it turn freely\n");

    int state = 0;                               // 0 = off, 1 = SW1 direction, 2 = SW2 direction
    int last_raw = -1;
    uint32_t rate = 0;
    while (1) {
        int s1 = !gpio_get_level(PIN_SW1), s2 = !gpio_get_level(PIN_SW2);
        if ((s1 | s2 << 1) != last_raw) {
            printf("SW1 %s  SW2 %s\n", s1 ? "CLOSED" : "open", s2 ? "CLOSED" : "open");
            last_raw = s1 | s2 << 1;
        }
        int want = (s1 && !s2) ? 1 : (s2 && !s1) ? 2 : 0;
        if (want != state) {
            step_rate(0);
            rate = 0;
            if (want) {
                gpio_set_level(PIN_DIR, want == 2);
                gpio_set_level(PIN_MOT_EN, 0);   // driver on: full run current
            } else {
                gpio_set_level(PIN_MOT_EN, 1);   // driver off: no holding torque
                tmc_print_status();
            }
            printf(want ? "Turning, direction %d\n" : "Off, shaft free\n", want);
            state = want;
        }
        if (state && rate < JOG_RATE) {
            rate = rate ? rate + JOG_ACCEL / 100 : JOG_START;
            if (rate > JOG_RATE) rate = JOG_RATE;
            step_rate(rate);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
