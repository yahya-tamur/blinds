#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "board.h"
#include "tmc2209.h"
#include "tests.h"

static volatile bool stalled;
static void IRAM_ATTR on_diag(void *arg) { stalled = true; }

void test_stallguard(void)
{
    step_init();
    tmc_uart_init();
    if (!tmc_setup(16, MOTOR_IRMS)) return;
    tmc_write(TMC_TPWMTHRS, 0);          // stay in StealthChop at every speed
    tmc_write(TMC_TCOOLTHRS, 0xFFFFF);   // StallGuard active at every speed
    tmc_write(TMC_SGTHRS, 0);            // no stall detection while we measure

    gpio_config_t in = { .pin_bit_mask = 1ULL << PIN_DIAG, .mode = GPIO_MODE_INPUT, .intr_type = GPIO_INTR_POSEDGE };
    ESP_ERROR_CHECK(gpio_config(&in));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_DIAG, on_diag, NULL));

    gpio_set_level(PIN_MOT_EN, 0);
    step_rate(3200);                     // 1 rev/s: StallGuard needs some speed to work
    printf("Spinning up, let the shaft run free...\n");
    vTaskDelay(pdMS_TO_TICKS(1500));     // StealthChop tunes itself during the first moves

    uint32_t sum = 0, v;
    int n = 0;
    for (int i = 0; i < 20; i++) {
        if (tmc_read(TMC_SG_RESULT, &v)) { sum += v; n++; }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    uint32_t free_run = n ? sum / n : 0;
    // DIAG fires when SG_RESULT <= 2 * SGTHRS. Fire at about 40 % of the free-running value.
    uint32_t sgthrs = free_run * 2 / 10;
    if (sgthrs > 255) sgthrs = 255;
    printf("Free-running SG_RESULT %u -> SGTHRS %u. Now pinch the shaft to stall it.\n", (unsigned)free_run, (unsigned)sgthrs);
    stalled = false;
    tmc_write(TMC_SGTHRS, sgthrs);

    while (1) {
        if (tmc_read(TMC_SG_RESULT, &v)) printf("SG_RESULT %3u  DIAG %d\n", (unsigned)v, gpio_get_level(PIN_DIAG));
        if (stalled) {
            step_rate(0);
            printf("STALL detected: motor stopped. Restarting in 3 s\n");
            vTaskDelay(pdMS_TO_TICKS(3000));
            stalled = false;
            step_rate(3200);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
