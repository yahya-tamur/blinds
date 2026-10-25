#ifndef MEMSTATS_H
#define MEMSTATS_H

// Call first thing in setup(): fills the unused stack with a marker pattern so
// the deepest point the stack ever reaches can be found later.
void initialize_memory_stats();

// Logs heap and stack use: now, peak since startup, and total available.
void log_memory_stats();

#endif
