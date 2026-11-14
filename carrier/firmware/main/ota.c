#include <string.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "motor.h"
#include "ota.h"

static const char *TAG = "ota";
static volatile bool busy;

bool ota_busy(void) { return busy; }

static void confirm(void *arg)
{
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK)
        ESP_LOGI(TAG, "New firmware confirmed after %d s: it stays", OTA_CONFIRM_S);
}

void ota_init(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGW(TAG, "First start of new firmware: confirming it in %d s unless it crashes or restarts first",
                 OTA_CONFIRM_S);
        static esp_timer_handle_t t;
        const esp_timer_create_args_t args = { .callback = confirm, .name = "ota_confirm" };
        if (esp_timer_create(&args, &t) == ESP_OK) esp_timer_start_once(t, OTA_CONFIRM_S * 1000000LL);
    }
}

static void restart(void *arg) { esp_restart(); }

static esp_err_t fail(httpd_req_t *req, const char *status, const char *msg)
{
    ESP_LOGE(TAG, "Update failed: %s", msg);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, msg);
}

static esp_err_t api_ota(httpd_req_t *req)
{
    if (motor_running() || motor_finishing()) return fail(req, "409 Conflict", "The motor is moving: try again when it stops");
    if (busy) return fail(req, "409 Conflict", "An update is already running");
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) return fail(req, "500 Internal Server Error", "No update partition (flash the OTA partition table over USB once)");
    if (req->content_len == 0 || req->content_len > part->size)
        return fail(req, "413 Content Too Large", "Wrong size: send build/carrier_fw.bin");

    busy = true;
    // A new upload while this firmware is still in its first minute: keep this one (it is running well
    // enough to take an upload), or ESP-IDF refuses the update (ESP_ERR_OTA_ROLLBACK_INVALID_STATE)
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "Confirmed the running firmware first");
    }
    ESP_LOGI(TAG, "Receiving %u bytes into %s", (unsigned)req->content_len, part->label);
    esp_ota_handle_t h;
    // Sequential writes erase the flash a sector at a time as data arrives, so no task is blocked for long
    esp_err_t err = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &h);
    if (err != ESP_OK) { busy = false; return fail(req, "500 Internal Server Error", esp_err_to_name(err)); }

    static char buf[4096];   // static: the server handles one request at a time
    size_t got = 0;
    int last_tenth = -1;
    while (got < req->content_len) {
        int r = httpd_req_recv(req, buf, sizeof buf);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) { esp_ota_abort(h); busy = false; return fail(req, "400 Bad Request", "Upload interrupted"); }
        if (got == 0 && (unsigned char)buf[0] != 0xE9) {      // ESP image magic byte
            esp_ota_abort(h);
            busy = false;
            return fail(req, "400 Bad Request", "Not a firmware image: send build/carrier_fw.bin");
        }
        err = esp_ota_write(h, buf, r);
        if (err != ESP_OK) { esp_ota_abort(h); busy = false; return fail(req, "500 Internal Server Error", esp_err_to_name(err)); }
        got += r;
        int tenth = (int)(got * 10 / req->content_len);
        if (tenth != last_tenth) { last_tenth = tenth; ESP_LOGI(TAG, "%d%%", tenth * 10); }
    }
    err = esp_ota_end(h);          // checks the image (checksum, chip type)
    if (err == ESP_OK) err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) { busy = false; return fail(req, "400 Bad Request", err == ESP_ERR_OTA_VALIDATE_FAILED ? "The image is damaged or for another chip" : esp_err_to_name(err)); }

    ESP_LOGI(TAG, "Update written to %s: restarting in 1 s", part->label);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "Update written: restarting");
    static esp_timer_handle_t t;   // restart after the reply has gone out; busy stays set until then
    const esp_timer_create_args_t args = { .callback = restart, .name = "ota_restart" };
    if (esp_timer_create(&args, &t) == ESP_OK) esp_timer_start_once(t, 1000000);
    return ESP_OK;
}

void ota_register(httpd_handle_t server)
{
    const httpd_uri_t u = { .uri = "/api/ota", .method = HTTP_POST, .handler = api_ota };
    httpd_register_uri_handler(server, &u);
}
