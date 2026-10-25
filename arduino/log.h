#ifndef LOG_H
#define LOG_H

#include <Arduino.h>

// Use Log.print / Log.println instead of Serial: everything goes to the Serial
// Monitor as before, and the most recent lines are also kept so the web pages
// can show them in the browser's console (sent with /status).
class LogPrint : public Print {
 public:
  size_t write(uint8_t c) override;
  using Print::write;
};

extern LogPrint Log;

#define LOG_LINES 16
#define LOG_LINE_LENGTH 96

// Lines are numbered from 0 as they're completed. log_next() is the number the
// next line will get; log_line(n) returns line n, or nullptr if it has already
// been pushed out by newer ones (only the last LOG_LINES are kept).
unsigned long log_next();
const char* log_line(unsigned long n);

#endif
