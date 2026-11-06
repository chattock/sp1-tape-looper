#ifndef CLICK438_H
#define CLICK438_H
#include <stdint.h>
/* One word: eight ordered 2-bit actions, count4, candidate2, stable2,
 * latched1, cancelled1. Owner/lane lives in the active request. Call under
 * the existing short IRQ lock; never scan ADC or do I/O under that lock. */
#define CLICK438_SAMPLES 2u
#define CLICK438_LATCH (1u<<24)
#define CLICK438_CANCEL (1u<<25)
static inline uint32_t click438_start(void){return CLICK438_LATCH;}
static inline uint32_t click438_sample(uint32_t s,int held,unsigned op){
 if(!held)return CLICK438_CANCEL;
 if(s&CLICK438_CANCEL)return s;
 unsigned c=(s>>20)&3u,n=(s>>22)&3u;
 if(op>2)op=3; /* unrelated rocker/volume is inert, never a neutral */
 if(c!=op){c=op;n=1;}else if(n<3)n++;
 s=(s&~(15u<<20))|(c<<20)|(n<<22);
 if(n<CLICK438_SAMPLES)return s;
 if(!op)return s&~CLICK438_LATCH;
 if(op==3||s&CLICK438_LATCH)return s;
 s|=CLICK438_LATCH;unsigned count=(s>>16)&15u;
 if(count<8){s|=op<<(count*2);s=(s&~(15u<<16))|((count+1)<<16);}
 return s;
}
static inline unsigned click438_pop(uint32_t *s){
 unsigned n=(*s>>16)&15u;if((*s&CLICK438_CANCEL)||!n){*s=0;return 0;}
 unsigned op=*s&3u;*s=(*s&~0xfffffu)|((*s&65535u)>>2)|((n-1)<<16);return op;
}
/* Drain pending actions as one ordered burst while retaining edge debounce.
 * Cancellation remains latched until the owner request finishes. */
static inline unsigned click440_batch(uint32_t *s,uint32_t *actions){
 if(*s&CLICK438_CANCEL){*actions=0;return 0;}
 unsigned count=(*s>>16)&15u;*actions=*s&65535u;*s&=~0xfffffu;return count;
}
#endif
