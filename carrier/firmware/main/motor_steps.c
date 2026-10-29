// Motor runs as in the Arduino version, without its planned stops: speed up, cruise, slow down to a
// stop after a fixed number of steps, then a short pause and a ramped back-off the other way (also
// after a cancel, which brakes as hard as the acceleration allows). A GPTimer interrupt times every
// step, so web traffic can't disturb the pace. The driver is set up before every run, since it forgets
// its settings whenever the 24 V supply is cut.
//
// Speed profile: like the Arduino, v^2 changes by 2a on every step, so the speed after k steps of
// speeding up is sqrt(2ak). k is the ramp index: the step's speed comes from ramp_half_us[k], and k
// steps are also exactly what it takes to stop from there. The table is filled in motor_init, because
// the S3's interrupts can't use the FPU.
#include <math.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_log.h"
#include "app_config.h"
#include "board.h"
#include "motor.h"
#include "tmc2209.h"

static const char *TAG = "motor";

typedef enum { IDLE, RUNNING, PAUSING, BACKING_OFF } phase_t;

static gptimer_handle_t timer;
// Fixed after motor_init
static uint32_t run_steps, backoff_steps;
static uint32_t top;               // ramp index at cruising speed
static uint16_t *ramp_half_us;     // [1..top]: half a step's length (us) at each ramp index
static uint32_t *ramp_time_us;     // [0..top]: time to run through ramp indices 1..k
// Shared with the interrupt
static volatile phase_t phase = IDLE;
static volatile uint32_t left;     // steps left in the current move
static volatile uint32_t k;        // ramp index of the current step
static volatile bool cancel_requested;
static bool step_high;
static int back_level;             // DIR level for the back-off
static int dir;                    // what the application sees: 1, -1, or 0 once the run is over
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

static void set_alarm(gptimer_handle_t t, uint32_t us)
{
    gptimer_alarm_config_t alarm = { .alarm_count = us, .flags.auto_reload_on_alarm = true };
    gptimer_set_alarm_action(t, &alarm);
}

// Two alarms per step: one raises STEP, the next lowers it. On the rising one it also handles a
// cancel, moves between phases, and picks this step's speed.
static bool on_alarm(gptimer_handle_t t, const gptimer_alarm_event_data_t *ev, void *ctx)
{
    if (step_high) {
        gpio_set_level(PIN_STEP, 0);
        step_high = false;
        left--;
        return false;
    }
    if (cancel_requested) {
        cancel_requested = false;
        if (phase == RUNNING && left > k) left = k;   // brake: just enough steps left to stop
    }
    if (left == 0) {
        if (phase == RUNNING) {
            gpio_set_level(PIN_DIR, back_level);
            phase = PAUSING;
            k = 0;
            set_alarm(t, MOTOR_BACKOFF_PAUSE_MS * 1000);
            return false;
        } else if (phase == PAUSING) {
            phase = BACKING_OFF;
            left = backoff_steps;
        } else {
            phase = IDLE;
            gptimer_set_alarm_action(t, NULL);   // done: no more alarms until the next run
            return false;
        }
    }
    // Slow down once the steps left are just enough to stop in; otherwise speed up to cruising speed
    if (left <= k) {
        if (k > 1) k--;
    } else if (k < top) {
        k++;
    }
    set_alarm(t, ramp_half_us[k]);
    gpio_set_level(PIN_STEP, 1);
    step_high = true;
    return false;
}

// Time for a move of n steps whose last step was at ramp index k0 (0 at rest)
static uint32_t move_us(uint32_t n, uint32_t k0)
{
    if (n == 0) return 0;
    if (n <= k0) return ramp_time_us[n];                       // already slowing down
    if (n >= (top - k0) + top) {                               // reaches cruising speed
        uint32_t cruise = n - (top - k0) - top;
        return ramp_time_us[top] - ramp_time_us[k0] + cruise * 2 * ramp_half_us[top] + ramp_time_us[top];
    }
    uint32_t peak = (n + k0) / 2;                              // speeds up part way, then slows down
    return ramp_time_us[peak] - ramp_time_us[k0] + ramp_time_us[peak];
}

void motor_init(void)
{
    gpio_config_t out = { .pin_bit_mask = (1ULL << PIN_MOT_EN) | (1ULL << PIN_DIR) | (1ULL << PIN_STEP),
                          .mode = GPIO_MODE_OUTPUT };
    ESP_ERROR_CHECK(gpio_config(&out));
    gpio_set_level(PIN_MOT_EN, 1);   // driver off between runs
    gpio_set_level(PIN_STEP, 0);
    tmc_uart_init();

    float per_rev = 200.0f * MOTOR_MICROSTEPS;
    float speed = MOTOR_REV_PER_S * per_rev;   // microsteps per second
    float accel = MOTOR_ACCEL * per_rev;       // microsteps per second^2
    run_steps = (uint32_t)(MOTOR_RUN_REVS * per_rev + 0.5f);
    backoff_steps = (uint32_t)(MOTOR_BACKOFF_REVS * per_rev + 0.5f);
    top = (uint32_t)ceilf(speed * speed / (2 * accel));
    ramp_half_us = malloc((top + 1) * sizeof *ramp_half_us);
    ramp_time_us = malloc((top + 1) * sizeof *ramp_time_us);
    ramp_time_us[0] = 0;
    for (uint32_t i = 1; i <= top; i++) {
        float v = fminf(sqrtf(2 * accel * i), speed);
        ramp_half_us[i] = (uint16_t)(500000.0f / v + 0.5f);
        ramp_time_us[i] = ramp_time_us[i - 1] + 2 * ramp_half_us[i];
    }

    gptimer_config_t cfg = { .clk_src = GPTIMER_CLK_SRC_DEFAULT, .direction = GPTIMER_COUNT_UP, .resolution_hz = 1000000 };
    ESP_ERROR_CHECK(gptimer_new_timer(&cfg, &timer));
    gptimer_event_callbacks_t cbs = { .on_alarm = on_alarm };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(timer, &cbs, NULL));
    ESP_ERROR_CHECK(gptimer_enable(timer));
    ESP_LOGI(TAG, "Runs: %.2f revolutions at %.2f rev/s, %.2f rev/s^2 (%lu ramp steps), back-off %.2f revolutions, "
             "%.1f s in all, %s", MOTOR_RUN_REVS, MOTOR_REV_PER_S, MOTOR_ACCEL, (unsigned long)top,
             MOTOR_BACKOFF_REVS, motor_total_ms() / 1000.0f, MOTOR_SPREADCYCLE ? "SpreadCycle" : "StealthChop");
}

void motor_run(int d)
{
    if (phase != IDLE) return;
    if (!tmc_setup(MOTOR_MICROSTEPS, MOTOR_IRMS)) {
        ESP_LOGE(TAG, "No reply from the driver (is the 24 V adapter plugged in?): run not started");
        return;
    }
    if (MOTOR_SPREADCYCLE) {
        tmc_write(TMC_GCONF, (1u << 2) | (1u << 6) | (1u << 7) | (1u << 8));   // tmc_setup's GCONF + en_SpreadCycle
    }
    gptimer_stop(timer);   // stopped already unless a run just ended; the error is harmless
    int level = d > 0 ? MOTOR_UP_DIR : !MOTOR_UP_DIR;
    gpio_set_level(PIN_DIR, level);
    back_level = !level;
    gpio_set_level(PIN_MOT_EN, 0);
    step_high = false;
    cancel_requested = false;
    left = run_steps;
    k = 0;
    phase = RUNNING;
    dir = d;
    gptimer_set_raw_count(timer, 0);
    set_alarm(timer, 1000);   // first step 1 ms from now, giving the driver time to switch on
    gptimer_start(timer);
    ESP_LOGI(TAG, "Run %s started", d > 0 ? "up" : "down");
}

void motor_cancel(void)
{
    cancel_requested = true;   // brakes to a stop; the back-off still happens
}

// Switches the driver off once the run is over (no holding torque, as in the Arduino version).
// Called by everything that asks about the motor, so whoever notices first does it.
static void check_ended(void)
{
    bool ended = false;
    taskENTER_CRITICAL(&lock);
    if (phase == IDLE && dir != 0) {
        dir = 0;
        ended = true;
    }
    taskEXIT_CRITICAL(&lock);
    if (ended) {
        gptimer_stop(timer);
        gpio_set_level(PIN_MOT_EN, 1);
        ESP_LOGI(TAG, "Run ended");
    }
}

uint32_t motor_remaining_ms(void)
{
    check_ended();
    phase_t p = phase;
    uint32_t n = left, k0 = k;
    uint32_t us = p == RUNNING     ? move_us(n, k0) + MOTOR_BACKOFF_PAUSE_MS * 1000 + move_us(backoff_steps, 0)
                  : p == PAUSING   ? MOTOR_BACKOFF_PAUSE_MS * 1000 + move_us(backoff_steps, 0)
                  : p == BACKING_OFF ? move_us(n, k0) : 0;
    return us / 1000;
}

bool motor_running(void) { check_ended(); return dir != 0; }
int motor_dir(void) { check_ended(); return dir; }
uint32_t motor_total_ms(void)
{
    return (move_us(run_steps, 0) + MOTOR_BACKOFF_PAUSE_MS * 1000 + move_us(backoff_steps, 0)) / 1000;
}
