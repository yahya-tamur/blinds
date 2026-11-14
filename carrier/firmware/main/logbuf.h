// Keeps the last LOG_LINES lines of the log (everything printed with ESP_LOGx) so the web pages can
// show them in the browser's console. Lines are numbered from 0 as they are completed.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>

#define LOG_LINES       32
#define LOG_LINE_LENGTH 120

void logbuf_init(void);            // call first: hooks into the ESP log output
int logbuf_printf(const char *fmt, ...);            // printf that also copies into the buffer (tests use it
int logbuf_vprintf(const char *fmt, va_list args);  // through tests.h, so their output reaches the web page)
uint32_t logbuf_next(void);        // number the next line will get
// Copies line n into out and returns true, or false if it has already been pushed out.
bool logbuf_line(uint32_t n, char *out, size_t size);
