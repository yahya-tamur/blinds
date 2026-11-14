#include <stdio.h>
// Motor moves: speed up, cruise, slow down to a stop after a given number of steps, then (for runs, not
// jogs) a short pause and a ramped back-off the other way, also after a cancel, which brakes as hard as the
// acceleration allows. travel.c decides how far each move goes (to a stop, or a jog) and
// cancels it early when the Hall count reaches the stop or the motor stalls. A GPTimer interrupt times every
// step, so web traffic can't disturb the pace. The driver is set up before every run, since it forgets
// its settings whenever the 24 V supply is cut. The relay connects the motor only for the run (board.h has
// the order), so between runs the coils are open and turning the blinds by hand can't pump the 24 V rail.
//
// Speed profile: like the Arduino, v^2 changes by 2a on every step, so the speed after k steps of
// speeding up is sqrt(2ak). k is the ramp index: the step's speed comes from ramp_half_us[k], and k
// steps are also exactly what it takes to stop from there. The table is filled in motor_init, because
// the S3's interrupts can't use the FPU.
#include <math.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "nvs.h"
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
static uint32_t move_steps;        // length of the current move
static bool move_backoff;          // the current move ends with the pause and back-off (up runs)
static volatile uint32_t steps_run; // steps done in the main part of the current move
static int dir;                    // what the application sees: 1, -1, or 0 once the run is over
static bool ending;                // check_ended is switching the driver and relay off
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
bool motor_reapply(void);
#define DRIVER_SETTLE_MS 150       // between switching the driver on and the first step

// Breadcrumbs in RTC memory, which keeps its contents through a reset-pin pulse or a crash but not through a
// real loss of power: where the last move had got to when the board restarted (motor_report_breadcrumb)
#define CRUMB_MAGIC 0xB11D5u
enum { CRUMB_IDLE, CRUMB_RELAY_ON, CRUMB_DRIVER_ON, CRUMB_STEPPING, CRUMB_DRIVER_OFF };
static RTC_NOINIT_ATTR uint32_t crumb_magic, crumb_stage, crumb_steps, crumb_count;
static void crumb(uint32_t stage) { crumb_magic = CRUMB_MAGIC; crumb_stage = stage; }

static char crumb_text[96] = "none";
const char *motor_breadcrumb(void) { return crumb_text; }

void motor_report_breadcrumb(void)
{
    static const char *names[] = { "idle", "relay just switched on", "driver just enabled", "stepping",
                                   "driver just switched off" };
    if (crumb_magic != CRUMB_MAGIC) {
        snprintf(crumb_text, sizeof crumb_text, "none (first start, or the power really dropped)");
    } else {
        snprintf(crumb_text, sizeof crumb_text, "motor was %s (move %lu, %lu steps in)",
                 crumb_stage < 5 ? names[crumb_stage] : "?", (unsigned long)crumb_count, (unsigned long)crumb_steps);
    }
    ESP_LOGI(TAG, "Breadcrumb: %s", crumb_text);
    crumb_magic = CRUMB_MAGIC;
    crumb_stage = CRUMB_IDLE;
    crumb_steps = 0;
}
// Runtime settings (RAM only, back to app_config.h at restart): for tests and measurements over Wi-Fi
static float run_irms = MOTOR_IRMS;
static bool run_spread = MOTOR_SPREADCYCLE;

static void build_ramp(float speed_rps, float accel_rps2);
static float run_speed = MOTOR_REV_PER_S, run_accel = MOTOR_ACCEL;

// Run current (in hundredths of an amp) and chopper mode are saved in NVS ("irms_ca", "spread")
static void load_settings(void)
{
    nvs_handle_t h;
    int32_t v;
    if (nvs_open("blinds", NVS_READONLY, &h) != ESP_OK) return;
    if (nvs_get_i32(h, "irms_ca", &v) == ESP_OK && v >= 20 && v <= lroundf(MOTOR_IRMS * 100)) run_irms = v / 100.0f;
    if (nvs_get_i32(h, "spread", &v) == ESP_OK && (v == 0 || v == 1)) run_spread = v;
    nvs_close(h);
    ESP_LOGI(TAG, "Settings: %.2f A, %s", run_irms, run_spread ? "SpreadCycle" : "StealthChop");
}

void motor_set(float irms, int spread)
{
    if (irms >= 0.2f && irms <= MOTOR_IRMS) run_irms = irms;
    if (spread == 0 || spread == 1) run_spread = spread;
    nvs_handle_t h;
    if (nvs_open("blinds", NVS_READWRITE, &h) == ESP_OK) {
        if (nvs_set_i32(h, "irms_ca", lroundf(run_irms * 100)) != ESP_OK || nvs_set_i32(h, "spread", run_spread) != ESP_OK ||
            nvs_commit(h) != ESP_OK) ESP_LOGE(TAG, "Couldn't save the motor settings");
        nvs_close(h);
    }
    ESP_LOGI(TAG, "Settings: %.2f A, %s (saved)", run_irms, run_spread ? "SpreadCycle" : "StealthChop");
}

float motor_irms(void) { return run_irms; }
bool motor_spreadcycle(void) { return run_spread; }

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
        if (phase == RUNNING) steps_run++;
        crumb_steps++;
        return false;
    }
    if (cancel_requested) {
        cancel_requested = false;
        if (phase == RUNNING && left > k) left = k;   // brake: just enough steps left to stop
    }
    if (left == 0) {
        if (phase == RUNNING && !move_backoff) {
            phase = IDLE;
            gptimer_set_alarm_action(t, NULL);
            return false;
        } else if (phase == RUNNING) {
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

// The speed profile for the given cruising speed (rev/s) and acceleration (rev/s^2); only while no move runs
static void build_ramp(float speed_rps, float accel_rps2)
{
    float per_rev = 200.0f * MOTOR_MICROSTEPS;
    float speed = speed_rps * per_rev, accel = accel_rps2 * per_rev;
    uint32_t n = (uint32_t)ceilf(speed * speed / (2 * accel));
    uint16_t *half = malloc((n + 1) * sizeof *half);
    uint32_t *time = malloc((n + 1) * sizeof *time);
    if (!half || !time) { free(half); free(time); ESP_LOGE(TAG, "No memory for a %lu-step ramp", (unsigned long)n); return; }
    time[0] = 0;
    for (uint32_t i = 1; i <= n; i++) {
        float v = fminf(sqrtf(2 * accel * i), speed);
        half[i] = (uint16_t)(500000.0f / v + 0.5f);
        time[i] = time[i - 1] + 2 * half[i];
    }
    free(ramp_half_us);
    free(ramp_time_us);
    ramp_half_us = half;
    ramp_time_us = time;
    top = n;
    run_speed = speed_rps;
    run_accel = accel_rps2;
}

float motor_speed(void) { return run_speed; }

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
    load_settings();
    gpio_config_t out = { .pin_bit_mask = (1ULL << PIN_MOT_EN) | (1ULL << PIN_DIR) | (1ULL << PIN_STEP) |
                                          (1ULL << PIN_RELAY),
                          .mode = GPIO_MODE_OUTPUT };
    ESP_ERROR_CHECK(gpio_config(&out));
    gpio_set_level(PIN_MOT_EN, 1);   // driver off between runs
    gpio_set_level(PIN_RELAY, 0);    // motor disconnected between runs
    gpio_set_level(PIN_STEP, 0);
    tmc_uart_init();

    float per_rev = 200.0f * MOTOR_MICROSTEPS;
    run_steps = (uint32_t)(MOTOR_RUN_REVS * per_rev + 0.5f);
    backoff_steps = (uint32_t)(MOTOR_BACKOFF_REVS * per_rev + 0.5f);
    build_ramp(MOTOR_REV_PER_S, MOTOR_ACCEL);

    gptimer_config_t cfg = { .clk_src = GPTIMER_CLK_SRC_DEFAULT, .direction = GPTIMER_COUNT_UP, .resolution_hz = 1000000 };
    ESP_ERROR_CHECK(gptimer_new_timer(&cfg, &timer));
    gptimer_event_callbacks_t cbs = { .on_alarm = on_alarm };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(timer, &cbs, NULL));
    ESP_ERROR_CHECK(gptimer_enable(timer));
    ESP_LOGI(TAG, "Runs: %.2f revolutions at %.2f rev/s, %.2f rev/s^2 (%lu ramp steps), back-off %.2f revolutions, "
             "%.1f s in all, %s", MOTOR_RUN_REVS, MOTOR_REV_PER_S, MOTOR_ACCEL, (unsigned long)top,
             MOTOR_BACKOFF_REVS, motor_total_ms() / 1000.0f, MOTOR_SPREADCYCLE ? "SpreadCycle" : "StealthChop");
}

void motor_run(int d) { motor_move(d, run_steps, true, MOTOR_REV_PER_S); }

uint32_t motor_backoff_steps(void) { return backoff_steps; }

uint32_t motor_default_steps(void) { return run_steps; }

void motor_move(int d, uint32_t steps, bool backoff, float speed)
{
    if (phase != IDLE || steps == 0) return;
    // Cruising speed for this move, reached in about half a second (never slower than MOTOR_ACCEL)
    if (speed < 0.1f) speed = 0.1f;
    if (speed > MOTOR_MAX_SPEED) speed = MOTOR_MAX_SPEED;
    if (fabsf(speed - run_speed) > 0.005f) build_ramp(speed, fmaxf(MOTOR_ACCEL, speed * 2));
    if (!tmc_setup(MOTOR_MICROSTEPS, run_irms)) {
        ESP_LOGE(TAG, "No reply from the driver (is the 24 V adapter plugged in?): run not started");
        return;
    }
    tmc_write(TMC_GSTAT, 0x7);       // clear the reset and error flags: travel.c checks them during the move
    if (run_spread) {
        tmc_write(TMC_GCONF, (1u << 2) | (1u << 6) | (1u << 7) | (1u << 8));   // tmc_setup's GCONF + en_SpreadCycle
    }
    gptimer_stop(timer);   // stopped already unless a run just ended; the error is harmless
    int level = d > 0 ? MOTOR_UP_DIR : !MOTOR_UP_DIR;
    gpio_set_level(PIN_DIR, level);
    back_level = !level;
    crumb_count++;
    crumb_steps = 0;
    crumb(CRUMB_RELAY_ON);
    gpio_set_level(PIN_RELAY, 1);    // connect the motor while the driver is still off
    vTaskDelay(pdMS_TO_TICKS(RELAY_SETTLE_MS));
    uint32_t gstat_relay = 0;          // which step resets the driver: the relay closing, or the driver switching on?
    if (tmc_read(TMC_GSTAT, &gstat_relay) && (gstat_relay & 1)) {
        ESP_LOGW(TAG, "Driver reset when the relay closed (driver still off): settings sent again");
        motor_reapply();
    }
    crumb(CRUMB_DRIVER_ON);
    gpio_set_level(PIN_MOT_EN, 0);
    // Let the supply settle before the first step. Switching the driver on can dip its supply enough to
    // reset it (seen on v0.2.0): then it would start on its defaults (8 microsteps, trimmer current,
    // StealthChop), so check its reset flag and send the settings again before stepping. StealthChop also
    // tunes its current at standstill in this pause.
    vTaskDelay(pdMS_TO_TICKS(DRIVER_SETTLE_MS));
    uint32_t gstat = 0;
    if (tmc_read(TMC_GSTAT, &gstat) && (gstat & 1)) {
        ESP_LOGW(TAG, "Driver reset as it switched on (supply dipped): settings sent again before the first step");
        motor_reapply();
        vTaskDelay(pdMS_TO_TICKS(DRIVER_SETTLE_MS));
    }
    step_high = false;
    cancel_requested = false;
    left = steps;
    move_steps = steps;
    move_backoff = backoff;
    steps_run = 0;
    k = 0;
    phase = RUNNING;
    dir = d;
    gptimer_set_raw_count(timer, 0);
    set_alarm(timer, 1000);   // first step 1 ms from now, giving the driver time to switch on
    crumb(CRUMB_STEPPING);
    gptimer_start(timer);
    ESP_LOGI(TAG, "Move %s started: %.2f motor turns at %.2f rev/s%s", d > 0 ? "up" : "down",
             steps / (200.0f * MOTOR_MICROSTEPS), run_speed, backoff ? ", then the back-off" : "");
}

// The driver restarted mid-move (its supply dipped): send its settings again so the move carries on
// with the right microsteps, current and mode. The STEP pulses keep coming meanwhile.
bool motor_reapply(void)
{
    if (!tmc_setup(MOTOR_MICROSTEPS, run_irms)) return false;
    if (run_spread) tmc_write(TMC_GCONF, (1u << 2) | (1u << 6) | (1u << 7) | (1u << 8));
    tmc_write(TMC_GSTAT, 0x7);
    return true;
}

void motor_cancel(void)
{
    cancel_requested = true;   // brakes to a stop; the back-off still happens
}

// Switches the driver off and disconnects the motor once the run is over (no holding torque, as in the
// Arduino version). Called by everything that asks about the motor, so whoever notices first does it.
// The run counts as going on until the relay is off, so a new run can't start in the meantime.
static void check_ended(void)
{
    bool ended = false;
    taskENTER_CRITICAL(&lock);
    if (phase == IDLE && dir != 0 && !ending) {
        ending = true;
        ended = true;
    }
    taskEXIT_CRITICAL(&lock);
    if (ended) {
        gptimer_stop(timer);
        crumb(CRUMB_DRIVER_OFF);
        gpio_set_level(PIN_MOT_EN, 1);
        vTaskDelay(pdMS_TO_TICKS(RELAY_SETTLE_MS));   // let the coil current die away before the contacts open
        gpio_set_level(PIN_RELAY, 0);
        crumb(CRUMB_IDLE);
        taskENTER_CRITICAL(&lock);
        dir = 0;
        ending = false;
        taskEXIT_CRITICAL(&lock);
        ESP_LOGI(TAG, "Run ended");
    }
}

bool motor_finishing(void)
{
    check_ended();
    phase_t p = phase;
    if (p == PAUSING || p == BACKING_OFF) return true;
    if (p == RUNNING) return cancel_requested || left <= k;   // braking or slowing down to the planned stop
    return dir != 0;                                          // stopped, relay still being switched off
}

uint32_t motor_remaining_ms(void)
{
    check_ended();
    phase_t p = phase;
    uint32_t n = left, k0 = k;
    uint32_t us = p == RUNNING     ? move_us(n, k0) + (move_backoff ? MOTOR_BACKOFF_PAUSE_MS * 1000 + move_us(backoff_steps, 0) : 0)
                  : p == PAUSING   ? MOTOR_BACKOFF_PAUSE_MS * 1000 + move_us(backoff_steps, 0)
                  : p == BACKING_OFF ? move_us(n, k0) : 0;
    return us / 1000;
}

bool motor_running(void) { check_ended(); return dir != 0; }
int motor_dir(void) { check_ended(); return dir; }
uint32_t motor_total_ms(void)
{
    uint32_t steps = move_steps ? move_steps : run_steps;
    bool backoff = move_steps ? move_backoff : true;
    return (move_us(steps, 0) + (backoff ? MOTOR_BACKOFF_PAUSE_MS * 1000 + move_us(backoff_steps, 0) : 0)) / 1000;
}

bool motor_main_move(void) { return phase == RUNNING; }
uint32_t motor_steps_run(void) { return steps_run; }
