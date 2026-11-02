#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "driver/pulse_cnt.h"
#include "esp_err.h"
#include "app_config.h"
#include "board.h"
#include "tmc2209.h"
#include "tests.h"

// Hall sensors + motor: moves the motor a known number of turns each way and checks what the Hall
// sensors saw. A GPIO interrupt on each sensor records the motor position (in microsteps) at every
// edge, so each move prints:
//   - every edge, with the motor angle it happened at and the PCNT count;
//   - Hall counts per motor turn (from that, motor turns per turn of the magnet gear);
//   - the spacing between edges: equal spacing means even magnets and a good quadrature phase;
//   - warnings for one sensor changing twice in a row (chatter, or a missed edge on the other one).
// After each out-and-back pair, the count should be where it started (within 1: the latches'
// hysteresis puts the edges in slightly different places in each direction).
//
// The moves are short and come back to where they started. Speed and current match the blinds runs.

#define HM_MOTOR_REVS  4.0f    // motor turns per move
#define HM_PAIRS       3       // out-and-back pairs
#define HM_MAX_EDGES   512

static const float per_rev = 200.0f * MOTOR_MICROSTEPS;

// Step generator: a GPTimer alarm raises and lowers STEP; the task sets the speed
static gptimer_handle_t timer;
static volatile uint32_t left;        // microsteps left in this move
static volatile uint32_t half_us;     // half a step's length, set by the task
static volatile int32_t pos;          // motor position in microsteps
static volatile int step_dir;         // +1 or -1
static bool step_high;

// Edges seen by the Hall interrupt
typedef struct { int32_t pos; uint8_t a, b; } edge_t;
static edge_t edges[HM_MAX_EDGES];
static volatile int n_edges;
static volatile int dropped;

static bool on_alarm(gptimer_handle_t t, const gptimer_alarm_event_data_t *ev, void *ctx)
{
    if (step_high) {
        gpio_set_level(PIN_STEP, 0);
        step_high = false;
        pos += step_dir;
        if (--left == 0) gptimer_set_alarm_action(t, NULL);
        return false;
    }
    gptimer_alarm_config_t alarm = { .alarm_count = half_us, .flags.auto_reload_on_alarm = true };
    gptimer_set_alarm_action(t, &alarm);
    gpio_set_level(PIN_STEP, 1);
    step_high = true;
    return false;
}

static void on_hall(void *arg)
{
    int i = n_edges;
    if (i >= HM_MAX_EDGES) {
        dropped++;
        return;
    }
    edges[i] = (edge_t){ pos, !gpio_get_level(PIN_HALL_A), !gpio_get_level(PIN_HALL_B) };   // 1 = ON
    n_edges = i + 1;
}

// Moves n microsteps in direction d (+1/-1) with a ramp like the blinds runs; returns when done
static void move(int32_t n, int d)
{
    const float vmax = MOTOR_REV_PER_S * per_rev, accel = MOTOR_ACCEL * per_rev, vmin = 200;
    gpio_set_level(PIN_DIR, d > 0 ? MOTOR_UP_DIR : !MOTOR_UP_DIR);
    step_dir = d;
    step_high = false;
    half_us = (uint32_t)(500000 / vmin);
    left = n;
    gptimer_set_raw_count(timer, 0);
    gptimer_alarm_config_t alarm = { .alarm_count = 1000, .flags.auto_reload_on_alarm = true };
    gptimer_set_alarm_action(timer, &alarm);
    gptimer_start(timer);
    while (left) {
        // speed from the steps done and the steps left, so it ramps up and down; updated every tick
        uint32_t l = left, done = n - l;
        float v = fminf(vmax, fminf(vmin + sqrtf(2 * accel * done), vmin + sqrtf(2 * accel * l)));
        half_us = (uint32_t)(500000 / v);
        vTaskDelay(1);
    }
    gptimer_stop(timer);
}

static void report(int from_edge, int to_edge, int32_t start_pos, uint8_t pa, uint8_t pb, int count0, int count1, float revs)
{
    printf("  %-6s %-9s %-4s %-4s %s\n", "edge", "motor deg", "A", "B", "");
    int last_sensor = -1;
    int32_t prev = start_pos, min_gap = INT32_MAX, max_gap = 0;
    int64_t sum_gap = 0;
    int gaps = 0, repeats = 0;
    for (int i = from_edge; i < to_edge; i++) {
        edge_t e = edges[i];
        int sensor = e.a != pa ? 0 : e.b != pb ? 1 : -1;     // which one changed (-1: neither, a glitch)
        bool repeat = sensor >= 0 && sensor == last_sensor;
        printf("  %-6d %9.1f %-4s %-4s %s\n", i - from_edge + 1, (e.pos - start_pos) * 360.0f / per_rev,
               e.a ? "ON" : "off", e.b ? "ON" : "off",
               sensor < 0 ? "<- no change seen (glitch)" : repeat ? "<- same sensor twice in a row" : "");
        if (i > from_edge) {
            int32_t gap = abs(e.pos - prev);
            if (gap < min_gap) min_gap = gap;
            if (gap > max_gap) max_gap = gap;
            sum_gap += gap;
            gaps++;
        }
        repeats += repeat;
        prev = e.pos;
        pa = e.a;
        pb = e.b;
        if (sensor >= 0) last_sensor = sensor;
    }
    int counts = count1 - count0;
    printf("  Hall count %+d over %.2f motor turns = %.2f counts per motor turn", counts, revs, fabsf(counts / revs));
    if (counts) printf(" -> %.3f motor turns per turn of the magnet gear\n", fabsf(HALL_COUNTS_PER_REV * revs / counts));
    else printf(" -> no counts: is the gear connected, and the sensors close enough?\n");
    if (gaps) {
        float mean = (float)sum_gap / gaps;
        printf("  Edge spacing: mean %.1f, min %ld, max %ld microsteps (%.1f / %.1f / %.1f motor deg), spread %+.0f%% / %+.0f%%\n",
               mean, (long)min_gap, (long)max_gap, mean * 360 / per_rev, min_gap * 360 / per_rev, max_gap * 360 / per_rev,
               (min_gap / mean - 1) * 100, (max_gap / mean - 1) * 100);
    }
    if (repeats) printf("  %d edges where the same sensor changed twice in a row\n", repeats);
}

void test_hall_motor(void)
{
    pcnt_unit_handle_t counter = hall_counter_init();
    ESP_ERROR_CHECK(gpio_set_intr_type(PIN_HALL_A, GPIO_INTR_ANYEDGE));
    ESP_ERROR_CHECK(gpio_set_intr_type(PIN_HALL_B, GPIO_INTR_ANYEDGE));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_HALL_A, on_hall, NULL));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_HALL_B, on_hall, NULL));

    gpio_config_t out = { .pin_bit_mask = (1ULL << PIN_MOT_EN) | (1ULL << PIN_DIR) | (1ULL << PIN_STEP) |
                                          (1ULL << PIN_RELAY),
                          .mode = GPIO_MODE_OUTPUT };
    ESP_ERROR_CHECK(gpio_config(&out));
    gpio_set_level(PIN_MOT_EN, 1);
    gpio_set_level(PIN_RELAY, 0);
    gpio_set_level(PIN_STEP, 0);
    gptimer_config_t cfg = { .clk_src = GPTIMER_CLK_SRC_DEFAULT, .direction = GPTIMER_COUNT_UP, .resolution_hz = 1000000 };
    ESP_ERROR_CHECK(gptimer_new_timer(&cfg, &timer));
    gptimer_event_callbacks_t cbs = { .on_alarm = on_alarm };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(timer, &cbs, NULL));
    ESP_ERROR_CHECK(gptimer_enable(timer));

    tmc_uart_init();
    if (!tmc_setup(MOTOR_MICROSTEPS, MOTOR_IRMS)) {
        printf("No reply from the driver (is the 24 V adapter plugged in?)\n");
        return;
    }
    if (MOTOR_SPREADCYCLE) {
        tmc_write(TMC_GCONF, (1u << 2) | (1u << 6) | (1u << 7) | (1u << 8));   // as motor_steps.c
    }

    printf("Hall + motor test: %d pairs of %.1f motor turns out and back at %.2f rev/s. Starting in 3 s.\n",
           HM_PAIRS, HM_MOTOR_REVS, MOTOR_REV_PER_S);
    vTaskDelay(pdMS_TO_TICKS(3000));
    gpio_set_level(PIN_RELAY, 1);                      // connect the motor while the driver is off
    vTaskDelay(pdMS_TO_TICKS(RELAY_SETTLE_MS));
    gpio_set_level(PIN_MOT_EN, 0);
    vTaskDelay(pdMS_TO_TICKS(100));

    int32_t steps = (int32_t)(HM_MOTOR_REVS * per_rev + 0.5f);
    for (int pair = 1; pair <= HM_PAIRS; pair++) {
        int start_count;
        ESP_ERROR_CHECK(pcnt_unit_get_count(counter, &start_count));
        for (int d = 1; d >= -1; d -= 2) {
            int count0, count1;
            ESP_ERROR_CHECK(pcnt_unit_get_count(counter, &count0));
            int e0 = n_edges;
            int32_t p0 = pos;
            uint8_t a0 = !gpio_get_level(PIN_HALL_A), b0 = !gpio_get_level(PIN_HALL_B);
            move(steps, d);
            vTaskDelay(pdMS_TO_TICKS(300));            // let the gear settle before reading the count
            ESP_ERROR_CHECK(pcnt_unit_get_count(counter, &count1));
            printf("\nPair %d, %s (%+.1f motor turns):\n", pair, d > 0 ? "up" : "down", d * HM_MOTOR_REVS);
            report(e0, n_edges, p0, a0, b0, count0, count1, d * HM_MOTOR_REVS);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        int end_count;
        ESP_ERROR_CHECK(pcnt_unit_get_count(counter, &end_count));
        printf("Pair %d: back at the start, the count is %+d from where it began%s\n", pair, end_count - start_count,
               abs(end_count - start_count) <= 1 ? " (OK)" : "  <- slipped or missed counts");
    }
    gpio_set_level(PIN_MOT_EN, 1);                     // driver off, then disconnect: the motor turns freely
    vTaskDelay(pdMS_TO_TICKS(RELAY_SETTLE_MS));
    gpio_set_level(PIN_RELAY, 0);
    if (dropped) printf("%d edges not recorded (buffer full)\n", dropped);
    printf("\nDone. The driver is off.\n");
}
