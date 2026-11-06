#ifndef TAKE_PREVIEW404_H
#define TAKE_PREVIEW404_H
#include <stdbool.h>
#include <stdint.h>
#include "led_count434.h"

/* Main/control thread only. One clock for all four lanes; no storage access. */
typedef struct {
    uint32_t started_ms, selection;
    bool active;
} take_preview404;

/* -1 leaves the existing display in charge. Zero is an intentionally dark
 * preview (empty without saved takes, between pulses, or the shared pause).
 * Input is each track's total take count, independent of selected position. */
static int take_preview404_step(take_preview404 *p,uint32_t now,const uint32_t ord[4],bool allowed)
{
 uint32_t maximum=0,selection=2166136261u;
 for(unsigned i=0;i<4;i++){if(ord[i]>=131071u)allowed=false;if(ord[i]>maximum)maximum=ord[i];selection=(selection^ord[i])*16777619u;}
 if(!allowed){p->active=false;return -1;}
 if(!p->active||p->selection!=selection){p->active=true;p->selection=selection;p->started_ms=now;}
 uint32_t phase=(uint32_t)(now-p->started_ms)%(maximum*LED_COUNT_PERIOD_MS+900u);
 if(phase>=maximum*LED_COUNT_PERIOD_MS||phase%LED_COUNT_PERIOD_MS>=LED_COUNT_PHASE_MS)return 0;
 unsigned pulse=phase/LED_COUNT_PERIOD_MS,mask=0;for(unsigned i=0;i<4;i++)if(pulse<ord[i])mask|=1u<<i;return (int)mask;
}
#endif
