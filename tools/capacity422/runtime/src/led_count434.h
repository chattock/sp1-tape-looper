#ifndef LED_COUNT434_H
#define LED_COUNT434_H
/* Shared FX/take count cadence, at the existing ~8ms control tick. */
#define LED_COUNT_PHASE_TICKS 18u
#define LED_COUNT_PERIOD_TICKS (2u*LED_COUNT_PHASE_TICKS)
#define LED_COUNT_PHASE_MS (8u*LED_COUNT_PHASE_TICKS)
#define LED_COUNT_PERIOD_MS (2u*LED_COUNT_PHASE_MS)
#endif
