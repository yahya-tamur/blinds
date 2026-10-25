#include "log.h"

LogPrint Log;

static char lines[LOG_LINES][LOG_LINE_LENGTH];
static unsigned long next_line = 0;
static char current[LOG_LINE_LENGTH];  // line being written, until its '\n'
static size_t current_len = 0;

size_t LogPrint::write(uint8_t c) {
  Serial.write(c);
  if (c == '\n') {
    current[current_len] = '\0';
    memcpy(lines[next_line % LOG_LINES], current, current_len + 1);
    next_line++;
    current_len = 0;
  } else if (c != '\r' && current_len < LOG_LINE_LENGTH - 1) {
    current[current_len++] = c;  // longer lines are cut short for the browser only
  }
  return 1;
}

unsigned long log_next() {
  return next_line;
}

const char* log_line(unsigned long n) {
  if (n >= next_line || next_line - n > LOG_LINES) {
    return nullptr;
  }
  return lines[n % LOG_LINES];
}
