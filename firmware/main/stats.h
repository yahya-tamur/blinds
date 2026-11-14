// Health numbers: free memory, Wi-Fi signal, uptime, why the board last restarted, and task stack
// headroom. health_log() writes them to the log (and so to the browser console via logbuf); /api/status
// sends them to the page (see web.c).
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t heap_free;      // bytes of internal RAM free now
    uint32_t heap_min;       // lowest free since startup
    uint32_t heap_largest;   // largest single free block
    bool wifi;               // connected, so rssi is valid
    int rssi;                // dBm: -30 excellent ... -67 good ... -80 poor
    uint32_t uptime_s;
    const char *reset;       // why the board last started: "power on", "task watchdog", ...
    const char *version;     // firmware version (git describe) and build date
} health_t;

void health_init(void);        // logs the version, the reset reason and the running partition
void health_get(health_t *s);
void health_log(void);         // two log lines: memory and Wi-Fi, then the tightest task stacks
