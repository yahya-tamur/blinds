// Minimal TMC2209 single-wire UART driver (datasheet register map)
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define TMC_GCONF       0x00
#define TMC_IFCNT       0x02
#define TMC_IOIN        0x06
#define TMC_IHOLD_IRUN  0x10
#define TMC_TPOWERDOWN  0x11
#define TMC_TSTEP       0x12
#define TMC_TPWMTHRS    0x13
#define TMC_TCOOLTHRS   0x14
#define TMC_VACTUAL     0x22
#define TMC_SGTHRS      0x40
#define TMC_SG_RESULT   0x41
#define TMC_CHOPCONF    0x6C
#define TMC_DRV_STATUS  0x6F

void tmc_uart_init(void);
void tmc_write(uint8_t reg, uint32_t value);
bool tmc_read(uint8_t reg, uint32_t *value);
// Checks the UART link, then sets StealthChop, internal current reference, microsteps and RMS current.
bool tmc_setup(int microsteps, float irms_amps);
void tmc_print_status(void);
