#include "allocate.h"
#include <limits.h>
static uint32_t get(const uint8_t*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
typedef struct{uint32_t first,blocks,next,pool,alignment,pool_lba;} scan;
static bool occupied(void*ctx,uint32_t id,const uint8_t b[32]){
 (void)id;scan*x=ctx;if(get(b)!=3||!get(b+20))return true;
 uint32_t first=get(b+4),blocks=get(b+8);if(!blocks||first>=x->pool||blocks>x->pool-first)return false;
 /* Advance within a scan rather than rescanning once for every adjacent
  * object. Records need not be in extent order: the outer loop repeats until
  * a full scan makes no progress, so an earlier record is never overlooked.
  * Each jump crosses an actual overlap, preserving exact first-fit results. */
 if((uint64_t)first<(uint64_t)x->next+x->blocks&&x->next<first+blocks){
  uint32_t end=first+blocks,pad=(0u-(x->pool_lba+end))&(x->alignment-1);
  x->next=pad>UINT32_MAX-end?UINT32_MAX:end+pad;
 }
 return true;
}
bool cat426_find_room_hint(pg422*s,pg422*old,uint32_t blocks,uint32_t alignment,uint32_t pool_lba,uint32_t extra_first,uint32_t extra_blocks,uint32_t hint,uint32_t*out){
 uint8_t h[32];if(!s||!old||s==old||!s->ready||!out||!blocks||!alignment||(alignment&(alignment-1))||!pg422_get(s,0,h)||get(h)!=1)return false;
 uint32_t pool=get(h+16);if(blocks>pool||pool_lba>UINT32_MAX-pool||extra_first>pool||extra_blocks>pool-extra_first)return false;
 /* Fail closed if either checkpoint cannot be read. A torn inactive root
  * requires repair before allocating; it is never silently treated as free. */
 if(hint>pool)hint=0;
 uint32_t pad0=(0u-(pool_lba+hint))&(alignment-1);
 uint32_t first=pad0>pool-hint?pool:hint+pad0;
 bool wrap=hint!=0;
again:
 while(first<=pool-blocks){scan x={first,blocks,first,pool,alignment,pool_lba};
  if(!pg422_walk(s,occupied,&x))return false;
  uint32_t previous_root=UINT32_MAX,previous_tail=0;
  for(unsigned slot=0;slot<2;slot++){
   /* Validate BOTH headers on every pass. Identical immutable trees need one
    * occupancy walk, not another walk of the very same reachable objects. */
   if(!pg422_checkpoint(old,s->io,slot))return false;
   bool same=(old->root==s->root&&old->tail==s->tail)||
             (old->root==previous_root&&old->tail==previous_tail);
   previous_root=old->root;previous_tail=old->tail;
   if(!same&&!pg422_walk(old,occupied,&x))return false;
  }
  if(extra_blocks&&(uint64_t)extra_first<(uint64_t)x.next+blocks&&x.next<extra_first+extra_blocks)x.next=extra_first+extra_blocks;
  if(x.next==first){*out=first;return true;}
  uint32_t pad=(0u-(pool_lba+x.next))&(alignment-1);if(pad>UINT32_MAX-x.next)return false;first=x.next+pad;
 }
 if(wrap){wrap=false;first=(0u-pool_lba)&(alignment-1);goto again;}
 return false;
}
bool cat422_find_room(pg422*s,pg422*old,uint32_t blocks,uint32_t alignment,uint32_t pool_lba,uint32_t extra_first,uint32_t extra_blocks,uint32_t*out){return cat426_find_room_hint(s,old,blocks,alignment,pool_lba,extra_first,extra_blocks,0,out);}
