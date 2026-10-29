// The switch reports moves rather than positions: from off to up or down starts a run (even if the
// switch is then left there), back to off cancels. Moved straight from one side to the other, it
// cancels first and starts the new direction on the next poll.
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
        .mode = GPIO_MODE_INPUT,          // 10k pull-ups are on the board
    };
    ESP_ERROR_CHECK(gpio_config(&in));
    // Wherever the switch is at startup is its starting position, so a switch left on doesn't start a run
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
    if (position != 0 && now != 0) {
        position = 0;
        event_post(CMD_CANCEL, SRC_SWITCH);
        return;
    }
    position = now;
    event_post(now == 0 ? CMD_CANCEL : now > 0 ? CMD_UP : CMD_DOWN, SRC_SWITCH);
}
