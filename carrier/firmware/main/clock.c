#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "app_config.h"
#include "board.h"
#include "clock.h"

#define DS3231_ADDR 0x68
#define RESYNC_US   (3600LL * 1000000)   // without internet time, re-read the DS3231 this often

static const char *TAG = "clock";
static i2c_master_dev_handle_t rtc;
static bool rtc_ok;          // DS3231 found
static bool valid;
static int64_t last_sync_us; // when the system clock was last set, from either source

static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t bin2bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

// Days since 1970-01-01 for a date in the Gregorian calendar (no timegm() in newlib)
static long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

// Reads the DS3231 into the system clock. Returns false if the DS3231 lost its time.
static bool load_from_rtc(void)
{
    uint8_t reg = 0x0F, status, t[7];
    if (i2c_master_transmit_receive(rtc, &reg, 1, &status, 1, 100) != ESP_OK) return false;
    if (status & 0x80) return false;     // oscillator stopped: power and backup cell were both lost
    reg = 0x00;
    if (i2c_master_transmit_receive(rtc, &reg, 1, t, 7, 100) != ESP_OK) return false;
    long days = days_from_civil(2000 + bcd2bin(t[6]), bcd2bin(t[5] & 0x1F), bcd2bin(t[4]));
    time_t utc = days * 86400 + bcd2bin(t[2] & 0x3F) * 3600 + bcd2bin(t[1]) * 60 + bcd2bin(t[0]);
    struct timeval tv = { .tv_sec = utc };
    settimeofday(&tv, NULL);
    last_sync_us = esp_timer_get_time();
    return true;
}

void clock_init(void)
{
    setenv("TZ", APP_TZ, 1);
    tzset();
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_SDA,
        .scl_io_num = PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,   // the module has 4.7k pull-ups
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));
    if (i2c_master_probe(bus, DS3231_ADDR, 100) != ESP_OK) {
        ESP_LOGE(TAG, "No DS3231 found: the time comes from the internet only");
        return;
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DS3231_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &rtc));
    rtc_ok = true;
    valid = load_from_rtc();
    if (valid) {
        struct tm lt;
        clock_local(&lt);
        ESP_LOGI(TAG, "Time from the DS3231: %04d-%02d-%02d %02d:%02d:%02d", lt.tm_year + 1900, lt.tm_mon + 1,
                 lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec);
    } else {
        ESP_LOGW(TAG, "The DS3231 lost its time: alarms wait for internet time");
    }
}

bool clock_valid(void) { return valid; }

void clock_local(struct tm *out)
{
    time_t now = time(NULL);
    localtime_r(&now, out);
}

void clock_set_utc(time_t utc)
{
    struct timeval tv = { .tv_sec = utc };
    settimeofday(&tv, NULL);
    last_sync_us = esp_timer_get_time();
    valid = true;
    if (!rtc_ok) return;
    struct tm g;
    gmtime_r(&utc, &g);
    uint8_t b[8] = { 0x00, bin2bcd(g.tm_sec), bin2bcd(g.tm_min), bin2bcd(g.tm_hour), (uint8_t)(g.tm_wday + 1),
                     bin2bcd(g.tm_mday), bin2bcd(g.tm_mon + 1), bin2bcd(g.tm_year % 100) };
    uint8_t status[2] = { 0x0F, 0x00 };   // clears the oscillator-stop flag: the time is good again
    if (i2c_master_transmit(rtc, b, sizeof b, 100) != ESP_OK || i2c_master_transmit(rtc, status, 2, 100) != ESP_OK) {
        ESP_LOGE(TAG, "Couldn't write the time to the DS3231");
    }
}

void clock_poll(void)
{
    if (rtc_ok && esp_timer_get_time() - last_sync_us > RESYNC_US) {
        last_sync_us = esp_timer_get_time();   // try again in an hour even if this read fails
        if (load_from_rtc()) valid = true;
    }
}
