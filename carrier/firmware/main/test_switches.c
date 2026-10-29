#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "board.h"
#include "tests.h"

void test_switches(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_SW1) | (1ULL << PIN_SW2),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,       // 10k pull-ups are on the board
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    printf("Switch test: press and release each switch\n");

    int last1 = -1, last2 = -1;
    while (1) {
        int s1 = gpio_get_level(PIN_SW1), s2 = gpio_get_level(PIN_SW2);
        if (s1 != last1) { printf("SW1 %s\n", s1 ? "open" : "CLOSED"); last1 = s1; }
        if (s2 != last2) { printf("SW2 %s\n", s2 ? "open" : "CLOSED"); last2 = s2; }
        vTaskDelay(pdMS_TO_TICKS(20));          // 20 ms polling also debounces
    }
}
