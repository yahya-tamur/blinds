#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "nvs.h"
#include "app_config.h"
#include "motor.h"
#include "tmc2209.h"
#include "travel.h"
#include "tests/tests.h"      // hall_counter_init, HALL_COUNTS_PER_REV

static const char *TAG = "travel";

// Microsteps per Hall count: 4 motor turns per sprocket turn, 12 counts per sprocket turn
#define STEPS_PER_COUNT  (200.0f * MOTOR_MICROSTEPS * MOTOR_TURNS_PER_SPROCKET / HALL_COUNTS_PER_REV)
#define STALL_GAPS       2.5f   // no Hall edge for this many counts' worth of steps: stalled (tests on v0.1.0)
#define START_EXTRA_GAPS 1.5f   // more before the first edge: gear play, and the clutch letting go
#define OVERRUN_COUNTS   2      // the Hall count stops a move that gets this many counts past where it should end
#define SAVE_AFTER_MS    5000   // save the position once it has been still this long
#define LATE_EDGE        1.4f   // log an edge that took more than this many counts' worth of steps

static pcnt_unit_handle_t counter;
static int offset;              // position = HALL_UP_SIGN * count + offset
static bool stop_set[2];
static float speeds[2] = { MOTOR_SPEED_DEFAULT, MOTOR_SPEED_DEFAULT };   // [STOP_TOP] = up, [STOP_BOTTOM] = down
static int stops[2];
static int saved_pos, last_pos;
static TickType_t changed_at;
// The current move
static int move_dir;            // 0 when no move is being watched
static bool has_target;
static int target;
static int last_count, edges;
static uint32_t edge_steps;
static bool status_logged;

// GSTAT, cleared at the start of each move: a reset flag means the driver restarted (its supply dipped)
// and lost its settings; uv_cp means its charge pump ran low on supply
static void check_gstat(const char *when)
{
    uint32_t g = 0;
    if (!tmc_read(TMC_GSTAT, &g)) { ESP_LOGW(TAG, "GSTAT %s: no reply from the driver", when); return; }
    if (g & 0x7)
        ESP_LOGW(TAG, "GSTAT %s: 0x%lX%s%s%s", when, (unsigned long)g, g & 1 ? " DRIVER RESET (supply dipped, settings lost)" : "",
                 g & 2 ? " DRIVER ERROR (overtemp or short)" : "", g & 4 ? " CHARGE PUMP UNDERVOLTAGE" : "");
    else
        ESP_LOGI(TAG, "GSTAT %s: clean (no reset, no error)", when);
}
static uint32_t last_drv;          // DRV_STATUS 1 s into the last move, for the test card on /update
static bool last_drv_valid;
static TickType_t move_started;
static TickType_t gstat_checked;
static int run_to;                  // 1 / -1 while a run to a stop is going (0 for jogs)
static bool retried, stalled;       // this run has already retried / just stalled
static bool jogging;                // the current move is a jog
static int driver_resets;           // during the current move

static int count_now(void)
{
    int c = 0;
    pcnt_unit_get_count(counter, &c);
    return c;
}

int travel_pos(void) { return HALL_UP_SIGN * count_now() + offset; }

bool travel_last_driver(uint32_t *status) { *status = last_drv; return last_drv_valid; }

static void save(const char *key, int value, bool set)
{
    nvs_handle_t h;
    if (nvs_open("blinds", NVS_READWRITE, &h) != ESP_OK) return;
    if (set) nvs_set_i32(h, key, value);
    else nvs_erase_key(h, key);
    nvs_commit(h);
    nvs_close(h);
}

void travel_init(void)
{
    counter = hall_counter_init();
    nvs_handle_t h;
    int32_t v;
    if (nvs_open("blinds", NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_i32(h, "pos", &v) == ESP_OK) offset = v;
        if (nvs_get_i32(h, "stop_top", &v) == ESP_OK) { stops[STOP_TOP] = v; stop_set[STOP_TOP] = true; }
        if (nvs_get_i32(h, "stop_bottom", &v) == ESP_OK) { stops[STOP_BOTTOM] = v; stop_set[STOP_BOTTOM] = true; }
        if (nvs_get_i32(h, "spd_up", &v) == ESP_OK && v >= 1 && v <= 80) speeds[STOP_TOP] = v / 10.0f;
        if (nvs_get_i32(h, "spd_down", &v) == ESP_OK && v >= 1 && v <= 80) speeds[STOP_BOTTOM] = v / 10.0f;
        nvs_close(h);
    }
    saved_pos = last_pos = offset;
    ESP_LOGI(TAG, "Position %d (saved), top stop %s, bottom stop %s", offset,
             stop_set[STOP_TOP] ? "set" : "not set", stop_set[STOP_BOTTOM] ? "set" : "not set");
}

float travel_speed(int dir) { return speeds[dir > 0 ? STOP_TOP : STOP_BOTTOM]; }

void travel_set_speed(int dir, float rps)
{
    int tenths = (int)(rps * 10 + 0.5f);
    if (tenths < 1) tenths = 1;
    if (tenths > (int)(MOTOR_MAX_SPEED * 10)) tenths = (int)(MOTOR_MAX_SPEED * 10);
    speeds[dir > 0 ? STOP_TOP : STOP_BOTTOM] = tenths / 10.0f;
    save(dir > 0 ? "spd_up" : "spd_down", tenths, true);
    ESP_LOGI(TAG, "%s speed set to %.1f rev/s", dir > 0 ? "Up" : "Down", tenths / 10.0f);
}

bool travel_get_stop(int which, int *pos)
{
    if (pos) *pos = stops[which];
    return stop_set[which];
}

bool travel_set_stop(int which)
{
    int p = travel_pos();
    int other = which == STOP_TOP ? STOP_BOTTOM : STOP_TOP;
    if (stop_set[other] && (which == STOP_TOP ? p <= stops[other] : p >= stops[other])) {
        ESP_LOGW(TAG, "Not saved: the %s stop must be %s the %s stop (%d)", which == STOP_TOP ? "top" : "bottom",
                 which == STOP_TOP ? "above" : "below", which == STOP_TOP ? "bottom" : "top", stops[other]);
        return false;
    }
    stops[which] = p;
    stop_set[which] = true;
    save(which == STOP_TOP ? "stop_top" : "stop_bottom", p, true);
    ESP_LOGI(TAG, "%s stop set at %d", which == STOP_TOP ? "Top" : "Bottom", p);
    return true;
}

void travel_clear_stop(int which)
{
    stop_set[which] = false;
    save(which == STOP_TOP ? "stop_top" : "stop_bottom", 0, false);
    ESP_LOGI(TAG, "%s stop cleared", which == STOP_TOP ? "Top" : "Bottom");
}

static void run_to_stop(int dir);

static void start(int dir, uint32_t steps, bool backoff, float speed, bool target_set, int to)
{
    stalled = false;
    if (motor_running() || steps == 0) return;
    jogging = false;
    move_dir = dir;
    has_target = target_set;
    target = to;
    last_count = count_now();
    edges = 0;
    edge_steps = 0;
    status_logged = false;
    move_started = gstat_checked = xTaskGetTickCount();
    driver_resets = 0;
    motor_move(dir, steps, backoff, speed);
    if (!motor_running()) move_dir = 0;    // didn't start (no driver)
}

void travel_run(int dir)
{
    retried = false;                       // a new run from a command (the retry calls run_to_stop directly)
    run_to = 0;
    run_to_stop(dir);
}

static void run_to_stop(int dir)
{
    int which = dir > 0 ? STOP_TOP : STOP_BOTTOM;
    if (!stop_set[which]) {
        // Without a stop there's no safe length to run: set it on the Adjust stops card first
        ESP_LOGW(TAG, "No %s stop set: not running (set it under Adjust stops)", dir > 0 ? "top" : "bottom");
        return;
    }
    int counts = (stops[which] - travel_pos()) * dir;
    if (counts <= 0) {
        ESP_LOGI(TAG, "Already at or past the %s stop (position %d, stop %d)", dir > 0 ? "top" : "bottom",
                 travel_pos(), stops[which]);
        return;
    }
    // Planned to end exactly at the stop; the Hall count only steps in if it overruns. The blinds always pull
    // down onto their clutch, so an up run goes the back-off further and then turns back by it (the load
    // settles on the clutch, not the motor); a down run stops exactly at the stop with no back-off. The
    // back-off (0.25 turn, ~0.75 count) is about the sensors' reversal hysteresis, so the count never sees it.
    float speed = retried ? MOTOR_RETRY_SPEED : travel_speed(dir);
    ESP_LOGI(TAG, "To the %s stop: %d counts at %.1f rev/s%s", dir > 0 ? "top" : "bottom", counts, speed,
             retried ? " (retry after a stall)" : "");
    start(dir, (uint32_t)(counts * STEPS_PER_COUNT) + (dir > 0 ? motor_backoff_steps() : 0), dir > 0, speed, true,
          stops[which] + dir * OVERRUN_COUNTS);
    if (motor_running()) run_to = dir;
}

void travel_jog(int dir, int counts)
{
    if (counts < 1 || counts > 4 * HALL_COUNTS_PER_REV) return;
    start(dir, (uint32_t)(counts * STEPS_PER_COUNT), false, MOTOR_JOG_SPEED, true,
          travel_pos() + dir * (counts + OVERRUN_COUNTS));
    jogging = move_dir != 0;
}

bool travel_is_jog(void)
{
    return move_dir != 0 && jogging;
}

void travel_poll(void)
{
    int pos = travel_pos();
    TickType_t now = xTaskGetTickCount();
    if (pos != last_pos || motor_running()) { last_pos = pos; changed_at = now; }
    if (pos != saved_pos && now - changed_at > pdMS_TO_TICKS(SAVE_AFTER_MS)) {   // still for a while: save it
        save("pos", pos, true);
        saved_pos = pos;
    }

    if (!move_dir) return;
    if (!motor_running()) {
        check_gstat("at the end");
        ESP_LOGI(TAG, "Move ended at position %d (%d Hall edges%s)", pos, edges,
                 driver_resets ? ", driver settings re-sent after a reset" : "");
        move_dir = 0;
        int dir = run_to;
        run_to = 0;
        if (dir && stalled && !retried) {          // stalled on the way to a stop: finish slowly, once
            retried = true;
            ESP_LOGW(TAG, "Finishing the run at %.1f rev/s after the stall", MOTOR_RETRY_SPEED);
            run_to_stop(dir);
        } else if (dir && stalled) {
            ESP_LOGE(TAG, "Stalled again on the slow retry: stopped at position %d", pos);
        }
        return;
    }
    if (!motor_main_move()) return;      // pausing or backing off: nothing to watch

    // Every 100 ms: did the driver restart (supply dip)? Then it runs on its defaults (8 microsteps, trimmer
    // current, StealthChop): send its settings again at once
    if (now - gstat_checked >= pdMS_TO_TICKS(100)) {
        gstat_checked = now;
        uint32_t g = 0;
        if (tmc_read(TMC_GSTAT, &g) && (g & 1)) {
            driver_resets++;
            bool ok = motor_reapply();
            ESP_LOGW(TAG, "Driver reset mid-move (supply dipped) at position %d: settings %s (reset %d this move)",
                     pos, ok ? "re-sent" : "NOT re-sent (no reply)", driver_resets);
            edge_steps = motor_steps_run();   // give it a fresh stall allowance after the hiccup
        }
    }

    // The driver's view one second in: current scale, open coils, stall reading (for the vibration hunt)
    if (!status_logged && now - move_started > pdMS_TO_TICKS(1000)) {
        status_logged = true;
        uint32_t st = 0;
        check_gstat("1 s in");
        if (tmc_read(TMC_DRV_STATUS, &st)) { last_drv = st; last_drv_valid = true; }
        if (last_drv_valid)
            ESP_LOGI(TAG, "Driver 1 s in: DRV_STATUS 0x%08lX (current %lu/31%s%s%s)", (unsigned long)st,
                     (unsigned long)(st >> 16 & 0x1F), st & (1u << 6) ? ", coil A open" : "",
                     st & (1u << 7) ? ", coil B open" : "", st & 0x3E ? ", FAULT" : "");
    }

    int c = count_now();
    uint32_t steps = motor_steps_run();
    if (c != last_count) {
        int moved = (c - last_count) * HALL_UP_SIGN * move_dir;   // positive when going the commanded way
        last_count = c;
        if (moved > 0) {
            float gap = (steps - edge_steps) / STEPS_PER_COUNT;   // in counts; 1.0 nominal, up to ~1.2 normally
            if (edges > 0 && gap > LATE_EDGE)
                ESP_LOGW(TAG, "Late edge at position %d: %.2f counts' worth of steps (slowing or slipping)", pos, gap);
            edges += moved;
            edge_steps = steps;
        }
        else if (moved < 0 && edges == 0 && steps > 3 * STEPS_PER_COUNT) {
            ESP_LOGE(TAG, "The Hall count runs the wrong way: HALL_UP_SIGN in app_config.h is backwards. Stopping.");
            motor_cancel();
            move_dir = 0;
            return;
        }
    }
    if (has_target && (move_dir > 0 ? pos >= target : pos <= target)) {
        ESP_LOGW(TAG, "Overran its planned end by %d counts (position %d): braking", OVERRUN_COUNTS, pos);
        motor_cancel();
        has_target = false;
        return;
    }
    float limit = STEPS_PER_COUNT * (STALL_GAPS + (edges == 0 ? START_EXTRA_GAPS : 0));
    if (steps - edge_steps > limit) {
        ESP_LOGW(TAG, "Stall: no Hall edge for %.1f counts' worth of steps (position %d). Stopping.",
                 (steps - edge_steps) / STEPS_PER_COUNT, pos);
        motor_cancel();
        has_target = false;                // keep watching until it has stopped, to log the end
        stalled = true;
        edge_steps = steps;                // no repeated stall messages while it brakes
    }
}
