// Firmware updates over Wi-Fi: POST the new build/carrier_fw.bin to /api/ota (the /update page does it).
// The new firmware goes to the other app partition and starts after a restart. Rollback: if it crashes or
// restarts before it has run OTA_CONFIRM_S, the bootloader goes back to the previous firmware.
#pragma once
#include <stdbool.h>
#include "esp_http_server.h"

#define OTA_CONFIRM_S 60

void ota_init(void);                         // call at startup: confirms new firmware after OTA_CONFIRM_S
void ota_register(httpd_handle_t server);    // adds POST /api/ota
bool ota_busy(void);                         // an update is being written: the motor must not start
