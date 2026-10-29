#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "board.h"
#include "tests.h"

#define DS3231_ADDR 0x68

static i2c_master_dev_handle_t rtc;
static volatile uint32_t sqw_edges;
static void IRAM_ATTR on_sqw(void *arg) { sqw_edges++; }

static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t bin2bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static void rtc_read(uint8_t reg, uint8_t *buf, size_t len)
{
    ESP_ERROR_CHECK(i2c_master_transmit_receive(rtc, &reg, 1, buf, len, 100));
}

static void rtc_write(uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t b[8] = { reg };
    memcpy(b + 1, data, len);
    ESP_ERROR_CHECK(i2c_master_transmit(rtc, b, len + 1, 100));
}

// Sets the clock to the time this program was compiled (__DATE__ "Oct 20 2026", __TIME__ "03:59:12").
static void set_to_build_time(void)
{
    static const char *mon = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char m[4] = { 0 };
    int d, y, hh, mm, ss;
    sscanf(__DATE__, "%3s %d %d", m, &d, &y);
    sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss);
    int month = (int)(strstr(mon, m) - mon) / 3 + 1;
    uint8_t t[7] = { bin2bcd(ss), bin2bcd(mm), bin2bcd(hh), 1, bin2bcd(d), bin2bcd(month), bin2bcd(y % 100) };
    rtc_write(0x00, t, 7);
}

void test_rtc(void)
{
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
        printf("No DS3231 at 0x68: check the module is plugged in the right way round\n");
        return;
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DS3231_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &rtc));

    uint8_t status;
    rtc_read(0x0F, &status, 1);
    if (status & 0x80) {
        printf("Oscillator-stop flag set: the clock lost power (first run, or the backup cell did not hold it).\n");
        printf("Setting the clock to the build time.\n");
        set_to_build_time();
        status &= (uint8_t)~0x80;
        rtc_write(0x0F, &status, 1);
    } else {
        printf("Clock kept running while powered off: the backup cell works.\n");
    }

    uint8_t ctrl = 0x00;                 // oscillator on, INTCN = 0, RS = 00 -> 1 Hz square wave on SQW
    rtc_write(0x0E, &ctrl, 1);
    gpio_config_t in = { .pin_bit_mask = 1ULL << PIN_RTC_SQW, .mode = GPIO_MODE_INPUT, .intr_type = GPIO_INTR_NEGEDGE };
    ESP_ERROR_CHECK(gpio_config(&in));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_RTC_SQW, on_sqw, NULL));

    while (1) {
        uint8_t t[7], temp[2];
        rtc_read(0x00, t, 7);
        rtc_read(0x11, temp, 2);
        printf("20%02d-%02d-%02d %02d:%02d:%02d  %.2f C  SQW pulses %u\n",
               bcd2bin(t[6]), bcd2bin(t[5] & 0x1F), bcd2bin(t[4]), bcd2bin(t[2] & 0x3F), bcd2bin(t[1]), bcd2bin(t[0]),
               (int8_t)temp[0] + (temp[1] >> 6) * 0.25, (unsigned)sqw_edges);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
