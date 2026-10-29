#ifndef MEMSTATS_H
#define MEMSTATS_H

// The board sets aside only 1 KB of stack (see fsp.ld), shared by the program
// and every interrupt, and answering a web request with an interrupt on top
// came close to filling it. So the program -- setup, loop and everything they
// call -- runs on its own stack of this size instead, and the 1 KB is left to
// interrupts.
#define PROGRAM_STACK_BYTES 4096

// Call first thing in setup(). Fills both stacks with a marker pattern, so the
// deepest point each ever reaches can be found later, then switches to the
// program's own stack and calls `program`, which must never return.
[[noreturn]] void run_on_program_stack(void (*program)());

// Logs heap and stack use: now, peak since startup, and total available.
void log_memory_stats();

#endif
