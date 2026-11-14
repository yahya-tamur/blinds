// NEC remote receiver for the tests (tests/bringup.c): RMT RX on PIN_IR, prints each decoded frame.
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/rmt_rx.h"
#include "esp_err.h"
#include "app_config.h"
#include "board.h"
#include "tests.h"

static bool near(uint32_t us, uint32_t target)
{
    return us > target * 7 / 10 && us < target * 13 / 10;
}

// Runs in the RMT interrupt: hand the received frame to the task.
static bool on_rx_done(rmt_channel_handle_t ch, const rmt_rx_done_event_data_t *ev, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR((QueueHandle_t)ctx, ev, &woken);
    return woken == pdTRUE;
}

// NEC protocol: 9 ms mark + 4.5 ms space, then 32 bits (560 us mark; 560 us space = 0, 1690 us space = 1).
// The receiver's output is low during a mark, so duration0 = mark, duration1 = space (1 tick = 1 us).
// Returns true for a good frame (a full NEC code or a repeat).
static bool decode_nec(const rmt_symbol_word_t *s, size_t n)
{
    if (n >= 1 && near(s[0].duration0, 9000) && near(s[0].duration1, 2250)) { printf("NEC repeat (button held)\n"); return true; }
    if (n < 33 || !near(s[0].duration0, 9000) || !near(s[0].duration1, 4500)) {
        // A short or broken frame from an NEC remote means a weak signal; a steady different shape is another protocol
        printf("Frame of %u pulses, not NEC. First marks/spaces (us):", (unsigned)n);
        for (size_t i = 0; i < n && i < 6; i++) printf(" %u/%u", s[i].duration0, s[i].duration1);
        printf("\n");
        return false;
    }
    // Receivers and cheap remotes stretch marks and shrink spaces by 100-200 us, so accept any plausible mark
    // and split 0 from 1 at 1.1 ms of space instead of matching each timing to +-30 %.
    uint32_t data = 0;
    for (int i = 0; i < 32; i++) {
        const rmt_symbol_word_t *b = &s[1 + i];
        unsigned mark = b->duration0, space = b->duration1;
        if (mark < 300 || mark > 900 || space < 250 || space > 2400) {
            printf("Bad bit %d (mark %u us, space %u us): weak or disturbed signal\n", i, mark, space);
            return false;
        }
        if (space > 1100) data |= 1UL << i;
    }
    uint8_t addr = data, naddr = data >> 8, cmd = data >> 16, ncmd = data >> 24;
    bool ok = (uint8_t)~cmd == ncmd;
    const char *role = cmd == IR_CMD_UP ? "  = up" : cmd == IR_CMD_DOWN ? "  = down" : cmd == IR_CMD_CANCEL ? "  = cancel" : "";
    printf("NEC address 0x%02X%s  command 0x%02X%s%s\n", addr, (uint8_t)~addr == naddr ? "" : " (extended)",
           cmd, ok ? "" : " (check failed)", ok ? role : "");
    return ok;
}

static rmt_channel_handle_t rx;
static QueueHandle_t q;

// Receiver setup and a wait for one frame, shared with tests/bringup.c
void ir_init(void)
{
    rmt_rx_channel_config_t cfg = {
        .gpio_num = PIN_IR,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000,                 // 1 tick = 1 us
        .mem_block_symbols = 64,
    };
    ESP_ERROR_CHECK(rmt_new_rx_channel(&cfg, &rx));
    gpio_pullup_en(PIN_IR);                       // helps the receiver's weak built-in pull-up
    q = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    rmt_rx_event_callbacks_t cbs = { .on_recv_done = on_rx_done };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx, &cbs, q));
    ESP_ERROR_CHECK(rmt_enable(rx));
}

// 1 = a good NEC frame (printed), 0 = some other frame, -1 = nothing within timeout_ms
int ir_receive(int timeout_ms)
{
    static rmt_symbol_word_t buf[64];
    static bool armed;
    rmt_receive_config_t rcv = {
        .signal_range_min_ns = 1250,              // shorter pulses are noise
        .signal_range_max_ns = 12000000,          // 12 ms of silence ends a frame
    };
    if (!armed) {                                 // re-arm for every frame; stays armed after a timeout
        ESP_ERROR_CHECK(rmt_receive(rx, buf, sizeof buf, &rcv));
        armed = true;
    }
    rmt_rx_done_event_data_t ev;
    if (xQueueReceive(q, &ev, timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms)) != pdTRUE) return -1;
    armed = false;
    return decode_nec(ev.received_symbols, ev.num_symbols) ? 1 : 0;
}
