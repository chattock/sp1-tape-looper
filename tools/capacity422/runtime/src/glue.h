#ifndef SP1_GLUE_H
#define SP1_GLUE_H
#include <stdint.h>
#include <stdbool.h>
/* One reversible session edit, normalized per target track. No audio ownership. */
typedef struct {uint32_t a,b,mark,epoch;uint8_t mask,song,pending,active,latch;} glue_state;
_Static_assert(sizeof(glue_state)==24,"bounded glue state");
static inline bool glue_bounds(const glue_state *s,uint32_t len,uint32_t *a,uint32_t *b,uint32_t *keep){
 if(len<3||s->a>=65536||s->b>=65536||s->a==s->b)return false;
 *a=(uint32_t)(((uint64_t)s->a*len)>>16);*b=(uint32_t)(((uint64_t)s->b*len)>>16);
 if(*a==*b)return false;
 *keep=*b>*a?len-(*b-*a):*a-*b;
 return *keep>=2&&*keep<len;
}
static inline uint32_t glue_source(uint32_t p,uint32_t a,uint32_t b){return b>a?(p<a?p:p+b-a):b+p;}
static inline uint32_t glue_span(uint32_t p,uint32_t a,uint32_t b,uint32_t keep,bool reverse){
 if(p>=keep)return 0; /* A geometry change must never underflow the run. */
 if(!reverse)return b>a&&p<a?a-p:keep-p;
 return b>a&&p>=a?p-a+1:p+1;
}
#endif
