#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "board.h"
#include "tmc2209.h"

// CRC from the TMC2209 datasheet (polynomial x^8 + x^2 + x + 1, bits taken LSB first)
static uint8_t crc8(const uint8_t *data, int len)
{
    uint8_t crc = 0;
    for (int i = 0; i < len; i++) {
        uint8_t b = data[i];
        for (int j = 0; j < 8; j++) {
            if ((crc >> 7) ^ (b & 0x01)) crc = (uint8_t)((crc << 1) ^ 0x07);
            else crc = (uint8_t)(crc << 1);
            b >>= 1;
        }
    }
    return crc;
}

void tmc_uart_init(void)
{
    uart_config_t cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(TMC_UART, 256, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(TMC_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(TMC_UART, PIN_TMC_TX, PIN_TMC_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

void tmc_write(uint8_t reg, uint32_t value)
{
    uint8_t f[8] = { 0x05, TMC_ADDR, (uint8_t)(reg | 0x80),
                     (uint8_t)(value >> 24), (uint8_t)(value >> 16), (uint8_t)(value >> 8), (uint8_t)value, 0 };
    f[7] = crc8(f, 7);
    uart_flush_input(TMC_UART);
    uart_write_bytes(TMC_UART, f, sizeof f);
    uart_wait_tx_done(TMC_UART, pdMS_TO_TICKS(10));
    uint8_t echo[8];   // single-wire bus: our own bytes come back on RX
    uart_read_bytes(TMC_UART, echo, sizeof echo, pdMS_TO_TICKS(10));
}

bool tmc_read(uint8_t reg, uint32_t *value)
{
    uint8_t req[4] = { 0x05, TMC_ADDR, reg, 0 };
    req[3] = crc8(req, 3);
    uart_flush_input(TMC_UART);
    uart_write_bytes(TMC_UART, req, sizeof req);
    uint8_t r[12];     // 4 echoed request bytes + 8 reply bytes
    int n = uart_read_bytes(TMC_UART, r, sizeof r, pdMS_TO_TICKS(20));
    if (n != (int)sizeof r) return false;
    const uint8_t *a = r + 4;
    if (a[0] != 0x05 || a[1] != 0xFF || a[2] != reg || crc8(a, 7) != a[7]) return false;
    *value = (uint32_t)a[3] << 24 | (uint32_t)a[4] << 16 | (uint32_t)a[5] << 8 | a[6];
    return true;
}

bool tmc_setup(int microsteps, float irms)
{
    uint32_t ioin = 0, cnt0 = 0, cnt1 = 0;
    if (!tmc_read(TMC_IOIN, &ioin)) {
        printf("No reply from the TMC2209. Is the 24 V adapter plugged in? (UART needs VM)\n");
        return false;
    }
    printf("TMC2209 version 0x%02X (expect 0x21)\n", (unsigned)(ioin >> 24));
    tmc_read(TMC_IFCNT, &cnt0);

    // GCONF: pdn_disable (UART owns PDN), mstep_reg_select (microsteps from MRES), multistep_filt.
    // I_scale_analog = 0: internal current reference, the module's VREF trimmer is ignored.
    // en_SpreadCycle = 0: StealthChop, which StallGuard4 needs.
    tmc_write(TMC_GCONF, (1u << 6) | (1u << 7) | (1u << 8));

    int mres = 0;                                   // 256 -> 0, 128 -> 1, ... 1 (full step) -> 8
    for (int m = 256; m > microsteps && mres < 8; m >>= 1) mres++;
    // CHOPCONF reset value 0x10000053 (toff 3, hstrt 5, interpolation on, vsense 0) with MRES replaced
    tmc_write(TMC_CHOPCONF, (0x10000053u & ~(0xFu << 24)) | ((uint32_t)mres << 24));

    // vsense = 0 -> full scale 0.325 V: I_rms = (CS + 1) / 32 * 0.325 / (Rsense + 0.02) / sqrt(2)
    int cs = (int)(32.0f * irms * 1.41421f * (TMC_RSENSE + 0.02f) / 0.325f + 0.5f) - 1;
    if (cs < 0) cs = 0;
    if (cs > 31) cs = 31;
    tmc_write(TMC_IHOLD_IRUN, (8u << 16) | ((uint32_t)cs << 8) | (uint32_t)(cs / 4));  // hold at a quarter: less heat at standstill
    tmc_write(TMC_TPOWERDOWN, 20);

    tmc_read(TMC_IFCNT, &cnt1);
    printf("Writes acknowledged: %u (expect 4), run current scale %d/31\n", (unsigned)((cnt1 - cnt0) & 0xFF), cs);
    return true;
}

void tmc_print_status(void)
{
    uint32_t s;
    if (!tmc_read(TMC_DRV_STATUS, &s)) { printf("DRV_STATUS read failed\n"); return; }
    printf("DRV_STATUS 0x%08X:%s%s%s%s current scale %u/31\n", (unsigned)s,
           (s & (1u << 1)) ? " OVERTEMP" : "", (s & (1u << 0)) ? " overtemp-warning" : "",
           (s & (0xFu << 2)) ? " SHORT" : "", (s & (1u << 31)) ? " standstill" : " moving",
           (unsigned)((s >> 16) & 0x1F));
}
