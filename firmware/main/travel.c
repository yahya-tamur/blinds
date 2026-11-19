#include <stdio.h>
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
// Motor direction check. The Hall sensors sit on the blinds' sprocket, so the count always tells which way the
// blinds really go, and the position, stops and end stops are on that scale whatever the motor wiring. If
// the motor was rewired (a coil swapped), Up turns it down: a move whose count runs WRONG_WAY_COUNTS against
// the command swaps the motor direction (saved) and starts again. A move that stalls before any Hall edge
// may be the swapped motor pushing into an end stop: a short probe with the direction swapped tells.
#define WRONG_WAY_COUNTS 2
#define PROBE_COUNTS     4

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
static int start_count, best_net;   // count at the move's start; furthest it got the commanded way
static bool restart_pending;        // the motor ran the wrong way: direction swapped, start this again once stopped
static bool probing;                // the current move is the direction probe (motor direction swapped for it)
static int probe_result;            // 1 = the swapped direction is right, -1 = it isn't, 0 = can't tell yet
static bool probed;                 // the current run or task has had its probe
static bool user_cancelled;         // stopped by a command: no probe, retry or restart after it
static int jog_counts;
// End stops
typedef enum { TASK_NONE, TASK_DETECT_UP, TASK_DETECT_DOWN, TASK_HOME } task_t;
static task_t task;
static bool ends_set;
static int ends[2];                 // [STOP_TOP], [STOP_BOTTOM]
static int found_top;               // while detecting: where the top end stop was
static int home_then;               // the run (1 / -1) to start once re-homed, or 0
static bool pos_known = true;       // false after a restart during a move, until re-homed
static bool pos_ok_saved = true;    // what NVS "pos_ok" says
static char task_result[96];

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

// Renumbers the position and the end stops by d counts so the bottom end stop is 0. The top and bottom stops
// keep their numbers: they are counted from the bottom end stop, so they stay right after the position is lost
// and found again by detecting.
static void shift_positions(int d)
{
    if (!d) return;
    offset += d;
    saved_pos += d;
    last_pos += d;
    save("pos", saved_pos, true);
    if (ends_set) {
        for (int w = STOP_TOP; w <= STOP_BOTTOM; w++) {
            ends[w] += d;
            save(w == STOP_TOP ? "end_top" : "end_bottom", ends[w], true);
        }
    }
    ESP_LOGI(TAG, "Position renumbered by %+d so the bottom end stop is 0 (the stops keep their numbers)", d);
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
        if (nvs_get_i32(h, "end_top", &v) == ESP_OK) {
            ends[STOP_TOP] = v;
            if (nvs_get_i32(h, "end_bottom", &v) == ESP_OK) { ends[STOP_BOTTOM] = v; ends_set = true; }
        }
        // 0 while a move is going or the blinds haven't been still for long since (no key: older firmware, trusted)
        if (nvs_get_i32(h, "pos_ok", &v) == ESP_OK && v == 0) pos_known = pos_ok_saved = false;
        nvs_close(h);
    }
    saved_pos = last_pos = offset;
    if (ends_set) shift_positions(-ends[STOP_BOTTOM]);   // end stops saved before they were numbered from 0
    ESP_LOGI(TAG, "Position %d (saved), top stop %s, bottom stop %s", offset,
             stop_set[STOP_TOP] ? "set" : "not set", stop_set[STOP_BOTTOM] ? "set" : "not set");
    if (ends_set) ESP_LOGI(TAG, "End stops at %d (top) and %d (bottom)", ends[STOP_TOP], ends[STOP_BOTTOM]);
    if (!pos_known)
        ESP_LOGW(TAG, "The board restarted during a move: position unknown. %s", ends_set
                 ? "The next run re-homes on the bottom end stop first." : "Detect the end stops so it can re-home (web page).");
}

bool travel_get_ends(int *top, int *bottom)
{
    if (top) *top = ends[STOP_TOP];
    if (bottom) *bottom = ends[STOP_BOTTOM];
    return ends_set;
}

bool travel_pos_known(void) { return pos_known; }

const char *travel_task(void)
{
    return task == TASK_DETECT_UP ? "detect_up" : task == TASK_DETECT_DOWN ? "detect_down" : task == TASK_HOME ? "home" : "";
}

const char *travel_task_result(void) { return task_result; }

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
    start_count = last_count;
    best_net = 0;
    user_cancelled = false;
    if (pos_ok_saved) {                    // a restart from here until the blinds are still again loses the position
        save("pos_ok", 0, true);
        pos_ok_saved = false;
    }
    motor_move(dir, steps, backoff, speed);
    if (!motor_running()) move_dir = 0;    // didn't start (no driver)
}

// Runs into an end stop at ENDSTOP_SPEED: the stall check ends the move there
static void run_into_end(int dir, int max_counts)
{
    start(dir, (uint32_t)(max_counts * STEPS_PER_COUNT), false, ENDSTOP_SPEED, false, 0);
}

static void end_task(const char *fmt, int a, int b)
{
    snprintf(task_result, sizeof task_result, fmt, a, b);
    task = TASK_NONE;
}

void travel_home(void)
{
    if (motor_running()) return;
    if (!ends_set) {
        ESP_LOGW(TAG, "Can't re-home: the end stops haven't been detected");
        return;
    }
    ESP_LOGI(TAG, "Re-homing: down into the bottom end stop at %.1f rev/s", ENDSTOP_SPEED);
    task = TASK_HOME;
    task_result[0] = '\0';
    probed = false;
    run_into_end(-1, ends[STOP_TOP] - ends[STOP_BOTTOM] + HOME_EXTRA_COUNTS);
    if (!move_dir) end_task("Re-homing didn't start (no reply from the driver).", 0, 0);
}

void travel_detect_ends(void)
{
    if (motor_running()) return;
    ESP_LOGI(TAG, "Detecting end stops: up into the top one at %.1f rev/s", ENDSTOP_SPEED);
    home_then = 0;
    task = TASK_DETECT_UP;
    task_result[0] = '\0';
    probed = false;
    run_into_end(1, ENDSTOP_MAX_COUNTS);
    if (!move_dir) end_task("Detecting didn't start (no reply from the driver).", 0, 0);
}

void travel_run(int dir)
{
    retried = false;                       // a new run from a command (the retry calls run_to_stop directly)
    probed = false;
    run_to = 0;
    if (!pos_known && ends_set) {          // first find the position again, then this run
        home_then = dir;
        travel_home();
        return;
    }
    if (!pos_known) ESP_LOGW(TAG, "Position may be wrong (restart during a move): running anyway. Detect the end stops to re-home.");
    run_to_stop(dir);
}

// A detect or re-home move has ended at pos; stall = it ran into something
static void task_move_ended(int pos, bool stall)
{
    if (task == TASK_DETECT_UP) {
        if (!stall) {
            ESP_LOGW(TAG, "Detecting end stops: no top end stop within %d counts (or stopped)", ENDSTOP_MAX_COUNTS);
            end_task("Stopped before reaching the top end stop: nothing saved.", 0, 0);
            return;
        }
        found_top = pos;
        ESP_LOGI(TAG, "Top end stop at %d; now down into the bottom one", pos);
        task = TASK_DETECT_DOWN;
        run_into_end(-1, ENDSTOP_MAX_COUNTS);
        if (!move_dir) end_task("Stopped at the top end stop: the move down didn't start. Nothing saved.", 0, 0);
    } else if (task == TASK_DETECT_DOWN) {
        if (!stall) {
            ESP_LOGW(TAG, "Detecting end stops: no bottom end stop within %d counts (or stopped)", ENDSTOP_MAX_COUNTS);
            end_task("Stopped before reaching the bottom end stop: nothing saved.", 0, 0);
            return;
        }
        if (found_top - pos < 3) {
            ESP_LOGW(TAG, "End stops only %d counts apart: not saved (stalled on the way?)", found_top - pos);
            end_task("The end stops came out only %d counts apart, so they weren't saved. Try again.", found_top - pos, 0);
            return;
        }
        ends[STOP_TOP] = found_top;
        ends[STOP_BOTTOM] = pos;
        ends_set = true;
        shift_positions(-pos);          // the bottom end stop is 0, everything else counts up from it
        save("end_top", ends[STOP_TOP], true);
        save("end_bottom", ends[STOP_BOTTOM], true);
        pos_known = true;
        ESP_LOGI(TAG, "End stops saved: top %d, bottom %d", ends[STOP_TOP], ends[STOP_BOTTOM]);
        end_task("Found both: %d counts apart.", found_top - pos, 0);
    } else if (task == TASK_HOME) {
        int dir = home_then;
        home_then = 0;
        if (!stall) {
            ESP_LOGW(TAG, "Re-homing stopped before the bottom end stop: position still unknown");
            end_task("Stopped before reaching the bottom end stop: position still unknown.", 0, 0);
            return;
        }
        offset += ends[STOP_BOTTOM] - pos;         // here is the bottom end stop
        pos_known = true;
        ESP_LOGI(TAG, "Re-homed: at the bottom end stop, position %d (was counting %d)", travel_pos(), pos);
        end_task("Re-homed on the bottom end stop.", 0, 0);
        if (dir) {
            retried = false;
            run_to_stop(dir);
        }
    }
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
    jog_counts = counts;
    start(dir, (uint32_t)(counts * STEPS_PER_COUNT), false, MOTOR_JOG_SPEED, true,
          travel_pos() + dir * (counts + OVERRUN_COUNTS));
    jogging = move_dir != 0;
}

void travel_cancel(void)
{
    if (!motor_running()) return;
    user_cancelled = true;
    motor_cancel();
}

// The move that just ended (dir) is to happen again: after the motor direction was swapped, or after a
// probe that found the direction right (then a run finishes at the retry speed, as after any stall)
static void redo_move(int dir, bool swapped)
{
    if (task == TASK_DETECT_UP || task == TASK_DETECT_DOWN) {
        if (swapped) travel_detect_ends();          // what it found so far came from moves the wrong way
        else run_into_end(dir, ENDSTOP_MAX_COUNTS);
        if (!move_dir && task != TASK_NONE) end_task("Detecting stopped: the next move didn't start. Nothing saved.", 0, 0);
    } else if (task == TASK_HOME) {
        run_into_end(-1, ends[STOP_TOP] - ends[STOP_BOTTOM] + HOME_EXTRA_COUNTS);
        if (!move_dir) end_task("Re-homing stopped: the next move didn't start. Position still unknown.", 0, 0);
    } else if (jogging) {
        travel_jog(dir, jog_counts);
    } else if (run_to) {
        int d = run_to;
        run_to = 0;
        if (!swapped) retried = true;
        run_to_stop(d);
    }
}

// What follows a move that has ended (stalled or not), as before the direction check
static void after_move(int pos)
{
    if (task != TASK_NONE) {
        task_move_ended(pos, stalled);
        return;
    }
    int dir = run_to;
    run_to = 0;
    if (dir && stalled && !retried) {          // stalled on the way to a stop: finish slowly, once
        retried = true;
        ESP_LOGW(TAG, "Finishing the run at %.1f rev/s after the stall", MOTOR_RETRY_SPEED);
        run_to_stop(dir);
    } else if (dir && stalled) {
        ESP_LOGE(TAG, "Stalled again on the slow retry: stopped at position %d", pos);
    }
}

static void start_probe(int dir, int pos)
{
    probed = true;
    motor_set_reversed(!motor_reversed(), false);
    ESP_LOGW(TAG, "Stalled before the first Hall edge: trying %d counts with the motor direction swapped (rewired motor?)",
             PROBE_COUNTS);
    probing = true;
    probe_result = 0;
    start(dir, (uint32_t)(PROBE_COUNTS * STEPS_PER_COUNT), false, MOTOR_JOG_SPEED, false, 0);
    if (!move_dir) {                           // didn't start: as if there had been no probe
        probing = false;
        motor_set_reversed(!motor_reversed(), false);
        stalled = true;
        after_move(pos);
    }
}

static void probe_ended(int dir, int pos)
{
    probing = false;
    if (probe_result > 0 && !user_cancelled) {
        ESP_LOGW(TAG, "The motor turns the other way now (rewired): direction swapped and saved. The stops and end "
                 "stops stay as they are (the Hall count follows the blinds).");
        motor_set_reversed(motor_reversed(), true);
        redo_move(dir, true);
        return;
    }
    motor_set_reversed(!motor_reversed(), false);   // back to the direction it had
    if (user_cancelled) { stalled = false; after_move(pos); return; }
    if (probe_result < 0) {
        ESP_LOGI(TAG, "The motor direction is right (the probe went the other way): carrying on");
        redo_move(dir, false);
        return;
    }
    ESP_LOGW(TAG, "The probe didn't move the blinds either way: treating it as a stall");
    stalled = true;
    after_move(pos);
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
    if (now - changed_at > pdMS_TO_TICKS(SAVE_AFTER_MS) && !motor_running()) {   // still for a while: save it
        if (pos != saved_pos) {
            save("pos", pos, true);
            saved_pos = pos;
        }
        if (pos_known && !pos_ok_saved) {
            save("pos_ok", 1, true);
            pos_ok_saved = true;
        }
    }

    if (!move_dir) return;
    if (!motor_running()) {
        check_gstat("at the end");
        ESP_LOGI(TAG, "Move ended at position %d (%d Hall edges%s)", pos, edges,
                 driver_resets ? ", driver settings re-sent after a reset" : "");
        int dir = move_dir;
        move_dir = 0;
        if (probing) {
            probe_ended(dir, pos);
        } else if (restart_pending) {
            restart_pending = false;
            if (user_cancelled) after_move(pos);
            else redo_move(dir, true);
        } else if (stalled && edges == 0 && !probed && !user_cancelled && (task != TASK_NONE || run_to)) {
            start_probe(dir, pos);
        } else {
            after_move(pos);
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
    int net = (c - start_count) * HALL_UP_SIGN * move_dir;   // counts gone the commanded way since the start
    if (net > best_net) best_net = net;
    if (probing) {
        if (probe_result == 0 && (net >= WRONG_WAY_COUNTS || net <= -WRONG_WAY_COUNTS)) {
            probe_result = net > 0 ? 1 : -1;
            motor_cancel();
        }
    } else if (!restart_pending && net <= -WRONG_WAY_COUNTS && best_net < WRONG_WAY_COUNTS) {
        ESP_LOGW(TAG, "The blinds went %d counts %s on %s: the motor turns the other way (rewired). Direction swapped "
                 "and saved; the stops and end stops stay as they are (the Hall count follows the blinds).",
                 -net, move_dir > 0 ? "down" : "up", move_dir > 0 ? "an up move" : "a down move");
        motor_set_reversed(!motor_reversed(), true);
        motor_cancel();
        has_target = false;
        restart_pending = true;
    }
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
