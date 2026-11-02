#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "board.h"
#include "tmc2209.h"
#include "tests.h"

// Bring-up test for a newly built board: checks every part in turn and ends with a pass/fail list.
// Automatic checks first (RTC, driver, idle levels), then steps that need a hand (switch, remote,
// magnets; each waits up to PROMPT_S seconds), then the relay and a short motor move, about two minutes
// in all; watch it with idf.py monitor. The switch answers the yes/no questions: up = yes.

#define PROMPT_S     20
#define DS3231_ADDR  0x68
#define BRINGUP_IRMS 1.0f        // gentler than MOTOR_IRMS: the motor may be loose on the bench
#define DRV_OLA      (1u << 6)   // DRV_STATUS open load, coil A / B (only checked while moving slowly)
#define DRV_OLB      (1u << 7)
#define DRV_FAULTS   ((0xFu << 2) | (1u << 1))   // shorts to GND or supply, overtemperature

static const char *names[16];
static int results[16];          // 1 pass, 0 fail, -1 skipped
static int n_checks;

static void report(const char *name, int result, const char *fmt, ...)
{
    names[n_checks] = name;
    results[n_checks++] = result;
    printf("%s  %s: ", result > 0 ? "PASS" : result == 0 ? "FAIL" : "SKIP", name);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

// ---------------- DS3231 ----------------
static i2c_master_dev_handle_t rtc;
static volatile uint32_t sqw_edges;
static void IRAM_ATTR on_sqw(void *arg) { sqw_edges++; }

static uint8_t bin2bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static bool rtc_read(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(rtc, &reg, 1, buf, len, 100) == ESP_OK;
}

static bool rtc_write(uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t b[8] = { reg };
    memcpy(b + 1, data, len);
    return i2c_master_transmit(rtc, b, len + 1, 100) == ESP_OK;
}

// Sets the clock to the time this program was compiled (__DATE__ "Oct 20 2026", __TIME__ "03:59:12")
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

static void check_rtc(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0, .sda_io_num = PIN_SDA, .scl_io_num = PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,   // 4.7k pull-ups on the board
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));
    if (i2c_master_probe(bus, DS3231_ADDR, 100) != ESP_OK) {
        report("RTC answers", 0, "no DS3231 at 0x68 (check U5's solder joints, R17/R18, the 3.3 V rail)");
        report("RTC backup cell", -1, "no RTC");
        report("RTC temperature", -1, "no RTC");
        report("RTC 1 Hz on SQW", -1, "no RTC");
        return;
    }
    i2c_device_config_t dev_cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = DS3231_ADDR,
                                    .scl_speed_hz = 100000 };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &rtc));
    report("RTC answers", 1, "DS3231 at 0x68");

    uint8_t status = 0;
    rtc_read(0x0F, &status, 1);
    if (status & 0x80) {
        // Expected on the very first power-up; on a later one it means the coin cell didn't keep it running
        report("RTC backup cell", 0, "oscillator-stop flag set: first power-up, or no CR2032 / bad BT1 joints. "
               "Set to the build time; power off for a minute and run again to check the cell");
        set_to_build_time();
        status &= (uint8_t)~0x80;
        rtc_write(0x0F, &status, 1);
    } else {
        report("RTC backup cell", 1, "the clock kept running while the board was off");
    }

    uint8_t temp[2] = { 0 };
    rtc_read(0x11, temp, 2);
    float c = (int8_t)temp[0] + (temp[1] >> 6) * 0.25f;
    report("RTC temperature", c > 5 && c < 50, "%.2f C", c);

    uint8_t ctrl = 0x00;                         // oscillator on, INTCN = 0, RS = 00: 1 Hz square wave on SQW
    rtc_write(0x0E, &ctrl, 1);
    gpio_config_t in = { .pin_bit_mask = 1ULL << PIN_RTC_SQW, .mode = GPIO_MODE_INPUT, .intr_type = GPIO_INTR_NEGEDGE,
                         .pull_up_en = GPIO_PULLUP_ENABLE };   // open drain, no pull-up on the board
    ESP_ERROR_CHECK(gpio_config(&in));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_RTC_SQW, on_sqw, NULL));
    sqw_edges = 0;
    vTaskDelay(pdMS_TO_TICKS(3000));
    report("RTC 1 Hz on SQW", sqw_edges >= 2 && sqw_edges <= 4, "%u pulses in 3 s (expect 3)", (unsigned)sqw_edges);
}

// ---------------- inputs ----------------
// Switch position, read the same three times 10 ms apart: 1 up (SW1), -1 down (SW2), 0 off, 2 both (a short)
static int switch_now(void)
{
    int last = 99;
    for (int same = 0; same < 3;) {
        int s1 = !gpio_get_level(PIN_SW1), s2 = !gpio_get_level(PIN_SW2);
        int p = s1 && s2 ? 2 : s1 ? 1 : s2 ? -1 : 0;
        same = p == last ? same + 1 : 1;
        last = p;
        if (same < 3) vTaskDelay(pdMS_TO_TICKS(10));
    }
    return last;
}

static bool wait_switch(int want)
{
    for (int i = 0; i < PROMPT_S * 20; i++) {
        if (switch_now() == want) return true;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return false;
}

// Asks a yes/no question: switch up = yes, down = no. -1 if nobody answers.
static int ask(const char *question)
{
    printf(">>> %s Push the switch UP for yes, DOWN for no.\n", question);
    wait_switch(0);
    for (int i = 0; i < PROMPT_S * 20; i++) {
        int p = switch_now();
        if (p == 1 || p == -1) { wait_switch(0); return p == 1; }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return -1;
}

static void check_inputs(void)
{
    gpio_config_t sw = { .pin_bit_mask = (1ULL << PIN_SW1) | (1ULL << PIN_SW2), .mode = GPIO_MODE_INPUT,
                         .pull_up_en = GPIO_PULLUP_ENABLE };   // no pull-ups on the board
    ESP_ERROR_CHECK(gpio_config(&sw));
    ir_init();                                   // sets up GPIO12 with its pull-up

    int p = switch_now();
    report("Switch at rest", p == 0, p == 0 ? "both contacts open" : p == 2 ? "SW1 and SW2 both closed: short?" :
           "a contact reads closed with the switch at rest (solder bridge on S1?)");
    int ir = gpio_get_level(PIN_IR);
    report("IR receiver at rest", ir == 1, ir ? "output high" : "output stuck low (U6 orientation, R4/C8, 3.3 V?)");

    printf(">>> Push the switch UP and hold it...\n");
    bool up = wait_switch(1);
    report("Switch up", up, up ? "SW1 (GPIO13) closed" : "not seen in %d s", PROMPT_S);
    printf(">>> Now push it DOWN and hold it...\n");
    bool down = wait_switch(-1);
    report("Switch down", down, down ? "SW2 (GPIO14) closed" : "not seen in %d s", PROMPT_S);
    wait_switch(0);

    printf(">>> Point a remote at the receiver and press any button...\n");
    int r = -1;
    for (int64_t until = xTaskGetTickCount() + pdMS_TO_TICKS(PROMPT_S * 1000); r <= 0 && xTaskGetTickCount() < until;)
        r = ir_receive(1000);
    report("IR remote", r > 0, r > 0 ? "NEC code received (address above; IR_ADDRESS in app_config.h)" :
           r == 0 ? "frames seen but none decoded as NEC (other protocol, or a weak signal)" : "nothing received");
}

static void check_hall(void)
{
    hall_counter_init();
    const int pins[2] = { PIN_HALL_A, PIN_HALL_B };
    const char *what[2] = { "Hall A", "Hall B" };
    for (int i = 0; i < 2; i++) {
        printf(">>> %s (U%s): hold one face of a magnet to it, then the other face, until it switches both ways...\n",
               what[i], i == 0 ? "7" : "8");
        bool seen[2] = { false, false };
        for (int t = 0; t < PROMPT_S * 100 && !(seen[0] && seen[1]); t++) {
            seen[gpio_get_level(pins[i])] = true;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        report(what[i], seen[0] && seen[1], seen[0] && seen[1] ? "read both low (on) and high (off)" :
               seen[0] ? "stayed low (on): try the magnet's other face" : "stayed high (off): no south pole seen, "
               "or no 3.3 V at the sensor");
    }
}

// ---------------- driver, relay, motor ----------------
// Moves slowly for ms and returns the DRV_STATUS read in the middle of it (0xFFFFFFFF if no reply)
static uint32_t move_and_read(int dir, int ms)
{
    uint32_t s = 0xFFFFFFFF;
    gpio_set_level(PIN_DIR, dir);
    gpio_set_level(PIN_MOT_EN, 0);
    step_rate(400);                              // 1/8 turn per second at 16 microsteps: open load shows when slow
    vTaskDelay(pdMS_TO_TICKS(ms / 2));
    tmc_read(TMC_DRV_STATUS, &s);
    vTaskDelay(pdMS_TO_TICKS(ms / 2));
    step_rate(0);
    gpio_set_level(PIN_MOT_EN, 1);
    return s;
}

static void relay(int on)
{
    // Only ever switched with EN high, so the contacts never carry coil current
    vTaskDelay(pdMS_TO_TICKS(RELAY_SETTLE_MS));
    gpio_set_level(PIN_RELAY, on);
    vTaskDelay(pdMS_TO_TICKS(RELAY_SETTLE_MS));
}

static void check_motor(void)
{
    step_init();                                 // EN high, relay on: turn it off again for the first check
    gpio_set_level(PIN_RELAY, 0);
    tmc_uart_init();
    if (!tmc_setup(16, BRINGUP_IRMS)) {
        report("Driver answers", 0, "no UART reply: 24 V plugged in? U4 orientation, R3, GPIO17/18");
        report("Relay off", -1, "no driver");
        report("Motor turns", -1, "no driver");
        return;
    }
    uint32_t ioin = 0;
    tmc_read(TMC_IOIN, &ioin);
    report("Driver answers", (ioin >> 24) == 0x21, "TMC2209 version 0x%02X (expect 0x21)", (unsigned)(ioin >> 24));
    // Open load is only reported in SpreadCycle: same GCONF as tmc_setup, plus en_SpreadCycle (bit 2)
    tmc_write(TMC_GCONF, (1u << 2) | (1u << 6) | (1u << 7) | (1u << 8));

    printf("Moving with the relay OFF: the driver should find both coils open (the motor must not move)...\n");
    uint32_t s = move_and_read(0, 1000);
    report("Relay off", s != 0xFFFFFFFF && (s & DRV_OLA) && (s & DRV_OLB),
           "DRV_STATUS 0x%08X:%s%s (expect open load on both: the relay disconnects the motor)", (unsigned)s,
           (s & DRV_OLA) ? " open A" : "", (s & DRV_OLB) ? " open B" : "");

    relay(1);
    printf("Moving with the relay ON: half a turn one way, then back...\n");
    uint32_t s1 = move_and_read(0, 4000);
    vTaskDelay(pdMS_TO_TICKS(300));
    uint32_t s2 = move_and_read(1, 4000);
    relay(0);
    uint32_t bad = (s1 | s2) & (DRV_OLA | DRV_OLB | DRV_FAULTS);
    report("Relay on, coils connected", s1 != 0xFFFFFFFF && s2 != 0xFFFFFFFF && !bad,
           "DRV_STATUS 0x%08X / 0x%08X%s", (unsigned)s1, (unsigned)s2,
           bad ? " (open load = relay contact or motor wire; short or overtemp = driver/wiring)" : ", no faults");
    int yes = ask("Did the motor turn half a turn and back?");
    report("Motor turns", yes > 0, yes > 0 ? "seen" : yes == 0 ? "not seen" : "no answer");
}

void test_bringup(void)
{
    printf("\n==== Carrier bring-up test ====\n"
           "Multimeter checks before the DevKit and StepStick go on (24 V adapter in, black probe on GND):\n"
           "+24V (C4 +) about 24 V, so Q1 conducts; 5V_BUCK (C16 +) 5.1-5.5 V, +5V (D4 band) 4.8-5.5 V.\n"
           "With the DevKit on: its 3V3 pin 3.2-3.4 V.\n"
           "Plug in the 24 V adapter and the motor; have a remote and a magnet at hand.\n\n");
    vTaskDelay(pdMS_TO_TICKS(2000));
    check_rtc();
    check_inputs();
    check_hall();
    check_motor();

    int fails = 0, skips = 0;
    printf("\n==== Summary ====\n");
    for (int i = 0; i < n_checks; i++) {
        printf("  %s  %s\n", results[i] > 0 ? "PASS" : results[i] == 0 ? "FAIL" : "SKIP", names[i]);
        fails += results[i] == 0;
        skips += results[i] < 0;
    }
    printf("%d checks: %d passed, %d failed, %d skipped\n", n_checks, n_checks - fails - skips, fails, skips);
}
