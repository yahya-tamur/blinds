// NEC IR remote on the RMT peripheral (same decoding as test_ir.c). Repeat frames sent while a button
// is held are ignored, so holding a button counts as one press.
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/rmt_rx.h"
#include "esp_err.h"
#include "esp_log.h"
#include "app_config.h"
#include "board.h"
#include "events.h"
#include "inputs.h"

static const char *TAG = "remote";

static bool near(uint32_t us, uint32_t target)
{
    return us > target * 7 / 10 && us < target * 13 / 10;
}

static bool on_rx_done(rmt_channel_handle_t ch, const rmt_rx_done_event_data_t *ev, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR((QueueHandle_t)ctx, ev, &woken);
    return woken == pdTRUE;
}

// Returns true for a full NEC frame (not a repeat) with valid checks, filling in address and command.
static bool decode_nec(const rmt_symbol_word_t *s, size_t n, uint8_t *addr, uint8_t *cmd)
{
    if (n < 33 || !near(s[0].duration0, 9000) || !near(s[0].duration1, 4500)) return false;
    uint32_t data = 0;
    for (int i = 0; i < 32; i++) {
        unsigned mark = s[1 + i].duration0, space = s[1 + i].duration1;
        if (mark < 300 || mark > 900 || space < 250 || space > 2400) return false;
        if (space > 1100) data |= 1UL << i;
    }
    *addr = data;
    *cmd = data >> 16;
    return (uint8_t)~*cmd == (uint8_t)(data >> 24);
}

static void remote_task(void *arg)
{
    rmt_rx_channel_config_t cfg = {
        .gpio_num = PIN_IR,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000,                 // 1 tick = 1 us
        .mem_block_symbols = 64,
    };
    rmt_channel_handle_t rx;
    ESP_ERROR_CHECK(rmt_new_rx_channel(&cfg, &rx));
    QueueHandle_t q = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    rmt_rx_event_callbacks_t cbs = { .on_recv_done = on_rx_done };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx, &cbs, q));
    ESP_ERROR_CHECK(rmt_enable(rx));
    rmt_receive_config_t rcv = {
        .signal_range_min_ns = 1250,              // shorter pulses are noise
        .signal_range_max_ns = 12000000,          // 12 ms of silence ends a frame
    };
    static rmt_symbol_word_t buf[64];
    while (1) {
        ESP_ERROR_CHECK(rmt_receive(rx, buf, sizeof buf, &rcv));
        rmt_rx_done_event_data_t ev;
        xQueueReceive(q, &ev, portMAX_DELAY);
        uint8_t addr, cmd;
        if (!decode_nec(ev.received_symbols, ev.num_symbols, &addr, &cmd)) continue;
#if IR_ADDRESS >= 0
        if (addr != IR_ADDRESS) {
            ESP_LOGD(TAG, "Ignored address 0x%02X command 0x%02X", addr, cmd);
            continue;
        }
#endif
        command_t c = cmd == IR_CMD_UP ? CMD_UP : cmd == IR_CMD_DOWN ? CMD_DOWN : cmd == IR_CMD_CANCEL ? CMD_CANCEL : 0;
        ESP_LOGI(TAG, "Address 0x%02X command 0x%02X%s", addr, cmd, c ? "" : " (not assigned)");
        if (c) event_post(c, SRC_REMOTE);
    }
}

void remote_start(void)
{
    xTaskCreate(remote_task, "remote", 4096, NULL, 5, NULL);
}
