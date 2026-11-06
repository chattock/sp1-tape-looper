#ifndef SP1_GLUE467_H
#define SP1_GLUE467_H
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#define GLUE467_CUTS 8u
#define GLUE467_SPANS (GLUE467_CUTS+1u)
typedef struct {uint32_t source,length;} glue467_span;
typedef struct {
 glue467_span spans[GLUE467_SPANS];
 uint32_t original,keep,epoch,phase,start;
 uint8_t count,depth;
} glue467_track;
typedef struct {uint16_t a,b;uint8_t mask;} glue467_cut;
typedef struct {
 glue467_track track[4];glue467_cut cuts[GLUE467_CUTS];
 uint32_t mark,request_epoch;uint16_t a,b;
 uint8_t mask,song,pending,latch,count;
 volatile uint8_t request,reset;
} glue467_state;
_Static_assert(sizeof(glue467_state)<=536,"reuse retired transfer-sector allocation; no audio buffer changes");
static inline void glue467_init(glue467_track *t,uint32_t length,uint32_t start){
 memset(t,0,sizeof(*t));t->original=t->keep=length;t->start=start;
 t->spans[0]=(glue467_span){0,length};t->count=1;
}
/* Append intersections with a virtual interval. Only control-time work. */
static inline bool glue467_slice(const glue467_track *in,glue467_track *out,uint32_t lo,uint32_t hi){
 uint32_t base=0;
 for(unsigned i=0;i<in->count;i++){
  uint32_t end=base+in->spans[i].length;
  uint32_t a=lo>base?lo:base,b=hi<end?hi:end;
  if(a<b){uint32_t source=in->spans[i].source+a-base,n=b-a;
   if(out->count&&out->spans[out->count-1].source+out->spans[out->count-1].length==source)out->spans[out->count-1].length+=n;
   else {if(out->count>=GLUE467_SPANS)return false;out->spans[out->count++]=(glue467_span){source,n};}
   out->keep+=n;
  }
  base=end;
 }
 return true;
}
static inline bool glue467_remove(glue467_track *t,uint16_t qa,uint16_t qb){
 uint32_t a=(uint32_t)(((uint64_t)qa*t->keep)>>16),b=(uint32_t)(((uint64_t)qb*t->keep)>>16);
 if(a==b)return false;
 uint32_t keep=b>a?t->keep-(b-a):a-b;if(keep<2)return false;
 glue467_track out={0};out.original=t->original;out.start=t->start;out.depth=t->depth+1;
 if(b>a){if(!glue467_slice(t,&out,0,a)||!glue467_slice(t,&out,b,t->keep))return false;out.phase=a%keep;}
 else {if(!glue467_slice(t,&out,b,a))return false;out.phase=0;}
 if(out.keep!=keep||!out.count)return false;
 *t=out;return true;
}
static inline bool glue467_build(glue467_track *t,const glue467_state *s,unsigned track,unsigned count){
 uint32_t length=t->original,start=t->start;glue467_init(t,length,start);
 for(unsigned i=0;i<count;i++)if(s->cuts[i].mask&(1u<<track))if(!glue467_remove(t,s->cuts[i].a,s->cuts[i].b))return false;
 return true;
}
/* Bounded scan once per refill, never in the per-sample mixer. */
static inline bool glue467_locate(const glue467_track *t,uint32_t phase,uint32_t *source,uint32_t *forward,uint32_t *back){
 if(phase>=t->keep)return false;
 for(unsigned i=0;i<t->count;i++){uint32_t n=t->spans[i].length;
  if(phase<n){*source=t->spans[i].source+phase;*forward=n-phase;*back=phase+1;return true;}phase-=n;
 }return false;
}
#endif
