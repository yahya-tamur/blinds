#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_err.h"
#include "board.h"
#include "tests.h"

// Hall sensors for the tests: two SS460S latches on PIN_HALL_A / PIN_HALL_B, open collector,
// output low (ON) while a south pole faces the branded face, released by a north pole.
// A PCNT unit counts quadrature in hardware (both directions, both edges of both sensors):
// with 6 alternating magnets the count goes 12 per sprocket turn (30 degrees each).
// v0.2.0 has 10k pull-ups on the board; the internal ones are on as well (v0.1.0 has none).

// Sets up both sensor inputs and returns a running quadrature counter (also used by the local tests)
pcnt_unit_handle_t hall_counter_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_HALL_A) | (1ULL << PIN_HALL_B),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,        // open-collector outputs; v0.1.0 has no pull-ups on the board
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    pcnt_unit_config_t unit = { .low_limit = -30000, .high_limit = 30000 };
    pcnt_unit_handle_t u;
    ESP_ERROR_CHECK(pcnt_new_unit(&unit, &u));
    pcnt_glitch_filter_config_t filter = { .max_glitch_ns = 10000 };   // ignore pulses under 10 us
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(u, &filter));

    // x4 quadrature: each channel counts its own edges, with the direction set by the other sensor
    pcnt_chan_config_t ca = { .edge_gpio_num = PIN_HALL_A, .level_gpio_num = PIN_HALL_B };
    pcnt_chan_config_t cb = { .edge_gpio_num = PIN_HALL_B, .level_gpio_num = PIN_HALL_A };
    pcnt_channel_handle_t a, b;
    ESP_ERROR_CHECK(pcnt_new_channel(u, &ca, &a));
    ESP_ERROR_CHECK(pcnt_new_channel(u, &cb, &b));
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    ESP_ERROR_CHECK(pcnt_unit_enable(u));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(u));
    ESP_ERROR_CHECK(pcnt_unit_start(u));
    return u;
}
