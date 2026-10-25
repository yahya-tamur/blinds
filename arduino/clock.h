#ifndef CLOCK_H
#define CLOCK_H

// Local time of day, in minutes / seconds since midnight
int get_minute();
long get_second();

void initialize_clock();

void set_clock(unsigned long epoch);

#endif
