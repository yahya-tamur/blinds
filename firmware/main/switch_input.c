// The momentary (ON)-OFF-(ON) switch reports presses: pushed to up or down it posts that command once;
// letting it spring back to off posts nothing. The control task decides what a press does (start a run,
// stop one going the other way, or nothing).
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "app_config.h"
#include "board.h"
#include "events.h"
#include "inputs.h"

static int position;      // last settled position: 1 up, -1 down, 0 off
static int last_read;     // what the pins showed last time
static int64_t read_since_us;

static int read_position(void)
{
    if (!gpio_get_level(SWITCH_UP_PIN)) return 1;
    if (!gpio_get_level(SWITCH_DOWN_PIN)) return -1;
    return 0;
}

void switch_init(void)
{
    gpio_config_t in = {
        .pin_bit_mask = (1ULL << SWITCH_UP_PIN) | (1ULL << SWITCH_DOWN_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,  // the switch goes straight to the pins, with no pull-ups on the board
    };
    ESP_ERROR_CHECK(gpio_config(&in));
    // Wherever the switch is at startup is its starting position, so a switch held at power-up doesn't start a run
    position = last_read = read_position();
}

void switch_poll(void)
{
    int now = read_position();
    int64_t t = esp_timer_get_time();
    if (now != last_read) {
        last_read = now;
        read_since_us = t;
        return;
    }
    if (now == position || t - read_since_us < SWITCH_SETTLE_MS * 1000LL) return;
    position = now;
    if (now != 0) event_post(now > 0 ? CMD_UP : CMD_DOWN, SRC_SWITCH);
}
