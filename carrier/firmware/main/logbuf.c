#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "logbuf.h"

static char lines[LOG_LINES][LOG_LINE_LENGTH];
static uint32_t next_line;
static char current[LOG_LINE_LENGTH];   // line being written, until its '\n'
static size_t current_len;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t original;

// Everything still goes to the serial port; a copy of each line, without colour codes, goes to the buffer.
static int hook(const char *fmt, va_list args)
{
    char text[LOG_LINE_LENGTH];
    va_list copy;
    va_copy(copy, args);
    vsnprintf(text, sizeof text, fmt, copy);
    va_end(copy);

    taskENTER_CRITICAL(&lock);
    bool escape = false;
    for (const char *c = text; *c; c++) {
        if (*c == '\033') { escape = true; continue; }
        if (escape) { if (*c == 'm') escape = false; continue; }
        if (*c == '\n') {
            current[current_len] = '\0';
            memcpy(lines[next_line % LOG_LINES], current, current_len + 1);
            next_line++;
            current_len = 0;
        } else if (*c != '\r' && current_len < LOG_LINE_LENGTH - 1) {
            current[current_len++] = *c;
        }
    }
    taskEXIT_CRITICAL(&lock);
    return original(fmt, args);
}

void logbuf_init(void)
{
    original = esp_log_set_vprintf(hook);
}

uint32_t logbuf_next(void)
{
    taskENTER_CRITICAL(&lock);
    uint32_t n = next_line;
    taskEXIT_CRITICAL(&lock);
    return n;
}

bool logbuf_line(uint32_t n, char *out, size_t size)
{
    bool ok = false;
    taskENTER_CRITICAL(&lock);
    if (n < next_line && next_line - n <= LOG_LINES) {
        strlcpy(out, lines[n % LOG_LINES], size);
        ok = true;
    }
    taskEXIT_CRITICAL(&lock);
    return ok;
}
