// NEC IR remote on the RMT peripheral (same decoding as tests/ir.c). Repeat frames sent while a button
// is held are ignored, so holding a button counts as one press.
//
// The buttons it obeys are set up on the web page (Set up remote) and saved in NVS: each is a remote's
// NEC address, a command code and what it does (up, down or stop). Every other frame is logged and ignored.
// Learn mode (remote_learn) takes the next frame from any remote without acting on it, so the page can
// assign it.
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/rmt_rx.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "board.h"
#include "events.h"
#include "inputs.h"

static const char *TAG = "remote";

#define LEARN_MS 15000   // learn mode ends by itself this long after the page last asked for it

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static remote_t remotes[MAX_REMOTES];
static int nremotes;
static remote_button_t buttons[MAX_REMOTE_BUTTONS];
static int count;
static uint32_t learn_id;        // which request the capture answers; increases with each remote_learn(true)
static TickType_t learn_until;   // learning while learn_active
static bool learn_active;
static remote_capture_t capture; // id 0 = nothing captured yet

static const char *action_name(int a) { return a > 0 ? "up" : a < 0 ? "down" : "stop"; }

static void save(void)
{
    remote_button_t copy[MAX_REMOTE_BUTTONS];
    remote_t rcopy[MAX_REMOTES];
    taskENTER_CRITICAL(&lock);
    int n = count, nr = nremotes;
    memcpy(copy, buttons, sizeof copy);
    memcpy(rcopy, remotes, sizeof rcopy);
    taskEXIT_CRITICAL(&lock);
    nvs_handle_t h;
    if (nvs_open("blinds", NVS_READWRITE, &h) != ESP_OK) return;
    esp_err_t e1 = n ? nvs_set_blob(h, "remote", copy, n * sizeof(remote_button_t)) : nvs_erase_key(h, "remote");
    esp_err_t e2 = nr ? nvs_set_blob(h, "remotes", rcopy, nr * sizeof(remote_t)) : nvs_erase_key(h, "remotes");
    if ((e1 != ESP_OK && e1 != ESP_ERR_NVS_NOT_FOUND) || (e2 != ESP_OK && e2 != ESP_ERR_NVS_NOT_FOUND) || nvs_commit(h) != ESP_OK)
        ESP_LOGE(TAG, "Couldn't save the remote buttons");
    nvs_close(h);
}

static int find_remote(uint8_t addr)
{
    for (int i = 0; i < nremotes; i++)
        if (remotes[i].addr == addr) return i;
    return -1;
}

// Adds a remote named "Remote N" (lowest N not in use). Call inside the lock. Its index, or -1 if full.
static int new_remote_locked(uint8_t addr)
{
    int i = find_remote(addr);
    if (i >= 0 || nremotes == MAX_REMOTES) return i;
    remote_t *r = &remotes[nremotes];
    r->addr = addr;
    for (int k = 1;; k++) {
        snprintf(r->name, sizeof r->name, "Remote %d", k);
        int j = 0;
        while (j < nremotes && strcmp(remotes[j].name, r->name)) j++;
        if (j == nremotes) break;
    }
    return nremotes++;
}

static void load(void)
{
    nvs_handle_t h;
    if (nvs_open("blinds", NVS_READONLY, &h) != ESP_OK) return;
    size_t size = sizeof buttons;
    if (nvs_get_blob(h, "remote", buttons, &size) == ESP_OK) count = size / sizeof(remote_button_t);
    size = sizeof remotes;
    if (nvs_get_blob(h, "remotes", remotes, &size) == ESP_OK) nremotes = size / sizeof(remote_t);
    nvs_close(h);
    for (int i = 0; i < nremotes; i++) remotes[i].name[REMOTE_NAME_LENGTH - 1] = '\0';
    for (int i = 0; i < count; i++) new_remote_locked(buttons[i].addr);   // every button's remote is listed
    if (count) ESP_LOGI(TAG, "%d remote buttons set up", count);
    else ESP_LOGW(TAG, "No remote buttons set up yet (web page: Set up remote)");
}

int remote_remotes(remote_t *out)
{
    taskENTER_CRITICAL(&lock);
    int n = nremotes;
    memcpy(out, remotes, n * sizeof *out);
    taskEXIT_CRITICAL(&lock);
    return n;
}

bool remote_new(uint8_t addr)
{
    taskENTER_CRITICAL(&lock);
    bool had = find_remote(addr) >= 0;
    int i = new_remote_locked(addr);
    remote_t r = i >= 0 ? remotes[i] : (remote_t){ 0 };
    taskEXIT_CRITICAL(&lock);
    if (i < 0) {
        ESP_LOGW(TAG, "Not added: already %d remotes", MAX_REMOTES);
        return false;
    }
    if (!had) {
        save();
        ESP_LOGI(TAG, "Added %s (address 0x%02X)", r.name, addr);
    }
    return true;
}

bool remote_rename(uint8_t addr, const char *name)
{
    if (!name[0]) return false;
    char old[REMOTE_NAME_LENGTH];
    taskENTER_CRITICAL(&lock);
    int i = find_remote(addr);
    if (i >= 0) {
        strlcpy(old, remotes[i].name, sizeof old);
        strlcpy(remotes[i].name, name, sizeof remotes[i].name);
    }
    taskEXIT_CRITICAL(&lock);
    if (i < 0) return false;
    save();
    ESP_LOGI(TAG, "Renamed %s to %s", old, name);
    return true;
}

void remote_delete(uint8_t addr)
{
    taskENTER_CRITICAL(&lock);
    int i = find_remote(addr);
    if (i >= 0) {
        memmove(&remotes[i], &remotes[i + 1], (nremotes - i - 1) * sizeof *remotes);
        nremotes--;
    }
    int kept = 0;
    for (int j = 0; j < count; j++)
        if (buttons[j].addr != addr) buttons[kept++] = buttons[j];
    bool changed = i >= 0 || kept != count;
    count = kept;
    taskEXIT_CRITICAL(&lock);
    if (!changed) return;
    save();
    ESP_LOGI(TAG, "Removed the remote at address 0x%02X and its buttons", addr);
}

int remote_list(remote_button_t *out)
{
    taskENTER_CRITICAL(&lock);
    int n = count;
    memcpy(out, buttons, n * sizeof *out);
    taskEXIT_CRITICAL(&lock);
    return n;
}

bool remote_add(uint8_t addr, uint8_t cmd, int action)
{
    bool ok = true, replaced = false;
    taskENTER_CRITICAL(&lock);
    int i = 0;
    while (i < count && !(buttons[i].addr == addr && buttons[i].cmd == cmd)) i++;
    if (i < count) replaced = true;
    else if (count < MAX_REMOTE_BUTTONS && new_remote_locked(addr) >= 0) count++;
    else ok = false;
    if (ok) buttons[i] = (remote_button_t){ .addr = addr, .cmd = cmd, .action = action };
    taskEXIT_CRITICAL(&lock);
    if (!ok) {
        ESP_LOGW(TAG, "Not added: already %d buttons or %d remotes", MAX_REMOTE_BUTTONS, MAX_REMOTES);
        return false;
    }
    save();
    ESP_LOGI(TAG, "%s address 0x%02X command 0x%02X: %s", replaced ? "Changed" : "Added", addr, cmd, action_name(action));
    return true;
}

void remote_remove(uint8_t addr, uint8_t cmd)
{
    bool found = false;
    taskENTER_CRITICAL(&lock);
    for (int i = 0; i < count; i++)
        if (buttons[i].addr == addr && buttons[i].cmd == cmd) {
            buttons[i] = buttons[--count];
            found = true;
            break;
        }
    taskEXIT_CRITICAL(&lock);
    if (!found) return;   // already gone: another browser may have removed it first
    save();
    ESP_LOGI(TAG, "Removed address 0x%02X command 0x%02X", addr, cmd);
}

uint32_t remote_learn(bool on)
{
    taskENTER_CRITICAL(&lock);
    if (on && !learn_active) learn_id++;   // a renewal while still waiting keeps the same id
    learn_active = on;
    learn_until = xTaskGetTickCount() + pdMS_TO_TICKS(LEARN_MS);
    uint32_t id = learn_id;
    taskEXIT_CRITICAL(&lock);
    return id;
}

bool remote_learning(remote_capture_t *last)
{
    taskENTER_CRITICAL(&lock);
    if (learn_active && (int32_t)(xTaskGetTickCount() - learn_until) >= 0) learn_active = false;
    bool on = learn_active;
    *last = capture;
    taskEXIT_CRITICAL(&lock);
    return on;
}

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

// Handles one press: taken by learn mode, or the command of a set-up button (0 if none)
static command_t handle_press(uint8_t addr, uint8_t cmd, bool *learned)
{
    command_t c = 0;
    taskENTER_CRITICAL(&lock);
    *learned = learn_active && (int32_t)(xTaskGetTickCount() - learn_until) < 0;
    if (*learned) {
        learn_active = false;   // one press per request
        capture = (remote_capture_t){ .id = learn_id, .addr = addr, .cmd = cmd };
    } else {
        for (int i = 0; i < count; i++)
            if (buttons[i].addr == addr && buttons[i].cmd == cmd)
                c = buttons[i].action > 0 ? CMD_UP : buttons[i].action < 0 ? CMD_DOWN : CMD_CANCEL;
    }
    taskEXIT_CRITICAL(&lock);
    return c;
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
    gpio_pullup_en(PIN_IR);                       // helps the receiver's weak built-in pull-up
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
        bool learned;
        command_t c = handle_press(addr, cmd, &learned);
        ESP_LOGI(TAG, "Address 0x%02X command 0x%02X%s", addr, cmd,
                 learned ? " (learned)" : c ? "" : " (not set up)");
        if (c) event_post(c, SRC_REMOTE);
    }
}

void remote_start(void)
{
    load();
    xTaskCreate(remote_task, "remote", 4096, NULL, 5, NULL);
}
