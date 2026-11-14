#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "app_config.h"
#include "clock.h"
#include "wifi.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

static const char *TAG = "wifi";
static volatile bool connected;

bool wifi_connected(void) { return connected; }

static void on_time_sync(struct timeval *tv)
{
    bool first = !clock_valid();
    clock_set_utc(tv->tv_sec);
    ESP_LOGI(TAG, "Clock synced with internet time%s", first ? " (alarms now active)" : "");
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (connected) ESP_LOGW(TAG, "Disconnected, reconnecting");
        connected = false;
        vTaskDelay(pdMS_TO_TICKS(2000));   // runs in the event task; nothing else waits on it meanwhile
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = data;
        connected = true;
        ESP_LOGI(TAG, "Connected, address " IPSTR " (http://" IPSTR "/)", IP2STR(&ev->ip_info.ip), IP2STR(&ev->ip_info.ip));
    }
}

void wifi_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
#ifndef SECRET_SSID
    ESP_LOGW(TAG, "No main/secrets.h: Wi-Fi is off (copy secrets.example.h to secrets.h and rebuild)");
#else
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(netif, APP_HOSTNAME);
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));
    wifi_config_t cfg = { 0 };
    strlcpy((char *)cfg.sta.ssid, SECRET_SSID, sizeof cfg.sta.ssid);
    strlcpy((char *)cfg.sta.password, SECRET_PASS, sizeof cfg.sta.password);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Connecting to %s", SECRET_SSID);

    // SNTP keeps trying in the background and re-syncs every hour once connected
    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    sntp.sync_cb = on_time_sync;
    ESP_ERROR_CHECK(esp_netif_sntp_init(&sntp));
#endif
}
