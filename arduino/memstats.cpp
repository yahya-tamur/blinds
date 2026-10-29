#include "memstats.h"
#include "log.h"

#include <Arduino.h>
#include <malloc.h>

// Set by the linker script (fsp.ld): the heap runs from __HeapBase up to
// __HeapLimit, and the board's 1 KB stack from __StackTop down to __StackLimit.
// After run_on_program_stack() only interrupts use that stack.
extern "C" char __HeapBase, __HeapLimit, __StackLimit, __StackTop;

static const uint32_t STACK_PAINT = 0xA5A5A5A5;

// Nothing stops the interrupt stack growing past its reserved 1 KB, down into
// the top of the heap's space (which is free unless the heap grows that far).
// So the marker pattern also goes this far below the reserved area, to measure
// how deep it really gets.
static const size_t STACK_CHECK_BELOW = 4096;

static uint32_t program_stack[PROGRAM_STACK_BYTES / 4] __attribute__((aligned(8)));
static uint32_t* const program_stack_top = program_stack + PROGRAM_STACK_BYTES / 4;

static uint32_t* irq_paint_start;  // lowest word painted below the board's stack

extern "C" void* _sbrk(int incr);

// Where the heap currently ends; everything above it is free for the stack.
static char* heap_end() {
  return (char*)_sbrk(0);
}

// Makes the program run on the process stack pointer (PSP), set to `top`, and
// jumps to `program`. Interrupts keep the main stack pointer (MSP) and the
// board's stack. Naked, so it has no frame of its own: the stack it was called
// on stops being used partway through.
__attribute__((naked, noreturn)) static void switch_stack(uint32_t* top, void (*program)()) {
  __asm volatile(
      "msr psp, r0\n"
      "mrs r2, control\n"
      "orr r2, r2, #2\n"  // CONTROL.SPSEL = 1: the program uses PSP from here on
      "msr control, r2\n"
      "isb\n"
      "bx r1\n");
}

void run_on_program_stack(void (*program)()) {
  char* start = &__StackLimit - STACK_CHECK_BELOW;
  // Keep clear of what the heap already uses
  if (start < heap_end() + 256) {
    start = heap_end() + 256;
  }
  irq_paint_start = (uint32_t*)((uintptr_t)start & ~(uintptr_t)3);
  // Stop a little below the current stack pointer to leave this function's own frame alone
  uint32_t* in_use = (uint32_t*)__get_MSP() - 16;
  for (uint32_t* p = irq_paint_start; p < in_use; p++) {
    *p = STACK_PAINT;
  }
  for (uint32_t* p = program_stack; p < program_stack_top; p++) {
    *p = STACK_PAINT;
  }

  switch_stack(program_stack_top, program);
}

// Deepest a stack has reached, in bytes below its top, judging by how much of
// the marker pattern from `bottom` up is still untouched.
static unsigned long stack_peak(uint32_t* bottom, uint32_t* top) {
  uint32_t* p = bottom;
  while (p < top && *p == STACK_PAINT) {
    p++;
  }
  return (char*)top - (char*)p;
}

void log_memory_stats() {
  struct mallinfo m = mallinfo();
  // uordblks: bytes currently allocated. arena: bytes the heap has grown to,
  // which it never gives back, so it's the peak.
  static char line[96];  // static: keeps it off the stack being measured
  snprintf(line, sizeof(line), "Memory: heap %u now, peak %u of %u B",
           (unsigned)m.uordblks, (unsigned)m.arena, (unsigned)(&__HeapLimit - &__HeapBase));
  Log.println(line);

  // Program stack in use right now, i.e. by loop() and this function: always
  // small, since the deep calls (answering a web request) have returned by now
  unsigned long now = (char*)program_stack_top - (char*)__get_PSP();
  unsigned long peak = stack_peak(program_stack, program_stack_top);

  // If the heap has since grown into the painted area, check from above it
  uint32_t* irq_bottom = irq_paint_start;
  if ((char*)irq_bottom < heap_end()) {
    irq_bottom = (uint32_t*)(((uintptr_t)heap_end() + 3) & ~(uintptr_t)3);
  }
  unsigned long irq_peak = stack_peak(irq_bottom, (uint32_t*)&__StackTop);
  unsigned long irq_reserved = &__StackTop - &__StackLimit;
  unsigned long irq_checked = &__StackTop - (char*)irq_paint_start;

  snprintf(line, sizeof(line), "Stack: program %lu now, peak %s%lu of %u B; interrupts peak %s%lu of %lu B%s",
           now, peak >= PROGRAM_STACK_BYTES ? ">=" : "", peak, PROGRAM_STACK_BYTES,
           irq_peak >= irq_checked ? ">=" : "", irq_peak, irq_reserved,
           peak >= PROGRAM_STACK_BYTES || irq_peak > irq_reserved ? " OVERFLOW" : "");
  Log.println(line);
}
