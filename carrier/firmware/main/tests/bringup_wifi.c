// The bring-up test over Wi-Fi, for a board in its enclosure or away from a USB cable. Joins the network and
// starts the web server (with /update, so the application can be put back over Wi-Fi), but none of the
// application's own drivers, which would fight the test for the RTC, IR receiver, driver UART and STEP pin.
// POST /api/bringup runs test_bringup once; its output goes to the serial port and to /api/status (log), which
// local/tools/weblog.py prints live. A second POST restarts the board, since the test sets its hardware up once.
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "logbuf.h"
#include "ota.h"
#include "stats.h"
#include "web.h"
#include "wifi.h"
#include "tests.h"

static const char *TAG = "bringup";
static volatile int state;      // 0 not run, 1 running, 2 done

static void run(void *arg)
{
    test_bringup();
    state = 2;
    ESP_LOGI(TAG, "Bring-up test finished. POST /api/bringup again to restart and rerun, or upload the application at /update.");
    vTaskDelete(NULL);
}

static void restart(void *arg) { esp_restart(); }

static esp_err_t api_bringup(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    if (state == 1) return httpd_resp_sendstr(req, "Already running");
    if (state == 2) {
        httpd_resp_sendstr(req, "Restarting to run it again: POST again once the board is back");
        static esp_timer_handle_t t;
        const esp_timer_create_args_t args = { .callback = restart, .name = "restart" };
        if (esp_timer_create(&args, &t) == ESP_OK) esp_timer_start_once(t, 1000000);
        return ESP_OK;
    }
    state = 1;
    xTaskCreate(run, "bringup", 8192, NULL, 5, NULL);
    return httpd_resp_sendstr(req, "Started");
}

void test_bringup_wifi(void)
{
    logbuf_init();
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    health_init();
    ota_init();          // confirms this firmware after a minute, so it survives a restart
    wifi_start();
    web_start();
    const httpd_uri_t u = { .uri = "/api/bringup", .method = HTTP_POST, .handler = api_bringup };
    if (web_server()) httpd_register_uri_handler(web_server(), &u);
    ESP_LOGI(TAG, "Bring-up over Wi-Fi: waiting for POST /api/bringup (local/tools/weblog.py --bringup)");
}
