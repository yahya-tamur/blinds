#include "memstats.h"
#include "log.h"

#include <Arduino.h>
#include <malloc.h>

// Set by the linker script (fsp.ld): the heap runs from __HeapBase up to
// __HeapLimit, and the 1 KB main stack from __StackTop down to __StackLimit.
// Interrupts use the same stack.
extern "C" char __HeapBase, __HeapLimit, __StackLimit, __StackTop;

static const uint32_t STACK_PAINT = 0xA5A5A5A5;

// Nothing stops the stack growing past its reserved 1 KB, down into the top of
// the heap's space (which is free unless the heap grows that far). So the
// marker pattern also goes this far below the reserved area, to measure how
// deep the stack really gets.
static const size_t STACK_CHECK_BELOW = 4096;

extern "C" void* _sbrk(int incr);

static uint32_t* paint_start;  // lowest word painted

// Where the heap currently ends; everything above it is free for the stack.
static char* heap_end() {
  return (char*)_sbrk(0);
}

void initialize_memory_stats() {
  char* start = &__StackLimit - STACK_CHECK_BELOW;
  // Keep clear of what the heap already uses
  if (start < heap_end() + 256) {
    start = heap_end() + 256;
  }
  paint_start = (uint32_t*)((uintptr_t)start & ~(uintptr_t)3);
  // Stop a little below the current stack pointer to leave this function's own frame alone
  uint32_t* in_use = (uint32_t*)__get_MSP() - 16;
  for (uint32_t* p = paint_start; p < in_use; p++) {
    *p = STACK_PAINT;
  }
}

// Deepest the stack has reached, in bytes below __StackTop
static unsigned long stack_peak() {
  // If the heap has since grown into the painted area, start above it
  uint32_t* p = paint_start;
  if ((char*)p < heap_end()) {
    p = (uint32_t*)(((uintptr_t)heap_end() + 3) & ~(uintptr_t)3);
  }
  while (p < (uint32_t*)&__StackTop && *p == STACK_PAINT) {
    p++;
  }
  return &__StackTop - (char*)p;
}

void log_memory_stats() {
  struct mallinfo m = mallinfo();
  unsigned long peak = stack_peak();
  // Stack in use right now, i.e. by loop() and this function: always small,
  // since the deep calls (answering a web request) have returned by now
  unsigned long now = &__StackTop - (char*)__get_MSP();
  unsigned long reserved = &__StackTop - &__StackLimit;
  unsigned long checked = &__StackTop - (char*)paint_start;
  // uordblks: bytes currently allocated. arena: bytes the heap has grown to,
  // which it never gives back, so it's the peak.
  static char line[96];  // static: keeps it off the stack being measured
  snprintf(line, sizeof(line), "Memory: heap %u now, peak %u of %u B; stack %lu now, peak %s%lu of %lu B%s",
           (unsigned)m.uordblks, (unsigned)m.arena, (unsigned)(&__HeapLimit - &__HeapBase),
           now, peak >= checked ? ">=" : "", peak, reserved, peak > reserved ? " OVERFLOW" : "");
  Log.println(line);
}
