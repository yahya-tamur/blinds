// Carrier board pin map (ESP32-S3-DevKitC-1 GPIO numbers)
#pragma once

#define PIN_MOT_EN   4    // TMC2209 EN, active low, 10k pull-up keeps the motor off at boot
#define PIN_STEP     5
#define PIN_DIR      6
#define PIN_INDEX    7    // TMC2209 INDEX output
#define PIN_SDA      8    // DS3231 (4.7k pull-ups on the module)
#define PIN_SCL      9
#define PIN_RTC_SQW  10   // DS3231 SQW/INT, open drain, pull-up on the module
#define PIN_DIAG     11   // TMC2209 DIAG output (StallGuard), high = stall or error
#define PIN_IR       12   // TSOP38238 output, low while it sees 38 kHz
#define PIN_SW1      13   // switch to GND, 10k pull-up + 1k/100nF filter on the board
#define PIN_SW2      14
#define PIN_HALL_A   15   // rev 2 board: J4 pin 3, Hall sensor A (open collector), 10k pull-up + 1k/100nF filter
#define PIN_HALL_B   16   // rev 2 board: J4 pin 4, Hall sensor B
#define PIN_TMC_TX   17   // UART1 TX, through 1k to PDN_UART
#define PIN_TMC_RX   18   // UART1 RX, direct to PDN_UART (single-wire bus)

#define TMC_UART     UART_NUM_1
#define TMC_ADDR     0       // MS1 = MS2 = GND
#define TMC_RSENSE   0.11f   // BTT TMC2209 V1.3 sense resistors, ohms

// StepperOnline 17HS19-2004S1: 2.0 A/phase, 1.6 ohm, 59 Ncm holding torque.
// 1.7 A RMS (~85 % of rating, close to this driver's 1.77 A ceiling) for short bursts of motion.
// Hold current is a quarter of that; disable the driver (EN high) between moves if nothing needs holding.
#define MOTOR_IRMS   1.7f
