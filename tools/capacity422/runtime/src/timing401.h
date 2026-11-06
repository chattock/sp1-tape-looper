#ifndef SP1_TIMING401_H
#define SP1_TIMING401_H
#include <stdint.h>
#include <string.h>
enum { T401_REQUEST,T401_READY,T401_ARM,T401_CAPTURE,T401_STOP,T401_DRAIN,T401_DURABLE,T401_PLAY_READY,T401_EVENTS };
#define T401_SPANS 7u
/* Replaces the removed periodic thread census; bounded scalar diagnostics.
 * Last successful event pairs + cumulative maxima, no serial hot-path output.
 * Timestamps are uptime milliseconds; valid bits distinguish absent events.
 * PLAY_READY is publication after priming, not an acoustic measurement. */
typedef struct {
 uint32_t sequence,song,track,path,pad_blocks,trigger_sample,wait_blocks,valid;
 uint32_t at[T401_EVENTS],last[T401_SPANS],maximum[T401_SPANS],count[T401_SPANS];
} timing401_trace;
_Static_assert(sizeof(timing401_trace)<=160,"timing must fit retired census RAM");
static inline void timing401_begin(volatile timing401_trace *t,unsigned song,unsigned track,uint32_t ms){
 t->sequence++;t->song=song;t->track=track;t->path=0;t->pad_blocks=0;t->trigger_sample=0;
 for(unsigned i=0;i<T401_EVENTS;i++)t->at[i]=0;
 for(unsigned i=0;i<T401_SPANS;i++)t->last[i]=0;
 t->at[T401_REQUEST]=ms;t->valid=1u<<T401_REQUEST;
}
static inline void timing401_span(volatile timing401_trace *t,unsigned span,unsigned a,unsigned b){
 if(!(t->valid&(1u<<a))||!(t->valid&(1u<<b)))return;
 uint32_t elapsed=t->at[b]-t->at[a];t->last[span]=elapsed;
 if(elapsed>t->maximum[span])t->maximum[span]=elapsed;
 t->count[span]++;
}
static inline void timing401_mark(volatile timing401_trace *t,unsigned event,uint32_t ms){
 if(event>=T401_EVENTS||!(t->valid&1u)||(t->valid&(1u<<event)))return;
 t->at[event]=ms;t->valid|=1u<<event;
 switch(event){
 case T401_READY:timing401_span(t,0,T401_REQUEST,event);break;
 case T401_ARM:timing401_span(t,1,T401_READY,event);break;
 case T401_CAPTURE:timing401_span(t,2,T401_ARM,event);break;
 case T401_DRAIN:timing401_span(t,3,T401_STOP,event);break;
 case T401_DURABLE:timing401_span(t,4,T401_DRAIN,event);timing401_span(t,5,T401_PLAY_READY,event);break;
 case T401_PLAY_READY:timing401_span(t,5,T401_DURABLE,event);timing401_span(t,6,T401_STOP,event);break;
 default:break;
 }
}
#endif
