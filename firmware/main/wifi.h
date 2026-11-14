// Joins the Wi-Fi network in secrets.h and keeps the clock synced from the internet.
// Never blocks: the blinds work the same without a network.
#pragma once
#include <stdbool.h>

void wifi_start(void);
bool wifi_connected(void);
