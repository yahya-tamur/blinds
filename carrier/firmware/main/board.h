// Carrier board pin map (ESP32-S3-DevKitC-1 GPIO numbers), v0.2.0. The same pins work on v0.1.0, where GPIO21
// is not connected and the internal pull-ups sit alongside the board's.
#pragma once

#define PIN_MOT_EN   4    // TMC2209 EN, active low, 10k pull-up keeps the motor off at boot
#define PIN_STEP     5
#define PIN_DIR      6
#define PIN_INDEX    7    // TMC2209 INDEX output
#define PIN_SDA      8    // DS3231 (4.7k pull-ups on the board)
#define PIN_SCL      9
#define PIN_RTC_SQW  10   // DS3231 SQW/INT, open drain, needs the internal pull-up
#define PIN_DIAG     11   // TMC2209 DIAG output (StallGuard), high = stall or error
#define PIN_IR       12   // TSOP38238 output, low while it sees 38 kHz (~30k pull-up inside, plus the internal one)
#define PIN_SW1      13   // momentary switch to GND, straight to the pin: needs the internal pull-up
#define PIN_SW2      14
#define PIN_HALL_A   15   // Hall sensor A (open collector), on the board with a 10k pull-up
#define PIN_HALL_B   16   // Hall sensor B, the same
#define PIN_TMC_TX   17   // UART1 TX, through 1k to PDN_UART
#define PIN_TMC_RX   18   // UART1 RX, direct to PDN_UART (single-wire bus)
#define PIN_RELAY    21   // motor relay K1, high = motor connected (10k pull-down keeps it off at boot)

#define TMC_UART     UART_NUM_1
#define TMC_ADDR     0       // MS1 = MS2 = GND
#define TMC_RSENSE   0.11f   // BTT TMC2209 V1.3 sense resistors, ohms

// YEJMKJ 60 mm NEMA 17: 2.3 A/phase, 1.5 ohm, 2.8 mH, 84 Ncm holding torque.
// 1.7 A RMS (~75 % of rating, close to this driver's 1.77 A ceiling) for short bursts of motion.
// Hold current is a quarter of that; disable the driver (EN high) between moves if nothing needs holding.
// The relay only switches while EN is high, so its contacts never break the coil current:
// EN high, relay on, RELAY_SETTLE_MS, EN low ... run ... EN high, RELAY_SETTLE_MS, relay off.
#define RELAY_SETTLE_MS 10   // HFD2 operate/release time is under 5 ms
#define MOTOR_IRMS   1.7f
