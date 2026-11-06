#ifndef SP1_DMA_WAIT_H
#define SP1_DMA_WAIT_H
#include <stdint.h>
#include <stdbool.h>
/* Caller supplies a wrapping monotonic cycle counter. Re-read END after the
 * deadline check: the thread may have been preempted while DMA completed. */
static inline bool sp1_dma_wait(volatile uint32_t *end,
                              volatile uint32_t *stop,
                              volatile uint32_t *stopped,
                              volatile uint32_t *enable,
                              uint32_t (*now)(void), uint32_t limit,
                              uint32_t stop_limit)
{
 uint32_t start=now();
 while (!*end && (uint32_t)(now()-start)<limit) {}
 bool done=*end!=0;
 if (!done) {
  *stopped=0; *stop=1; start=now();
  while (!*stopped && (uint32_t)(now()-start)<stop_limit) {}
 }
 *enable=0;
 return done;
}
#endif
