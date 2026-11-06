#ifdef SP1_MAINTENANCE_CRC504
#include "../../import504/crc.h"
#endif
#include "store.h"
#include <string.h>
#include <limits.h>
static uint32_t u32(const uint8_t*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint8_t*p,uint32_t x){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(x>>(8*i));}
static uint32_t crc(const uint8_t*p){
#ifdef SP1_MAINTENANCE_CRC504
 return ~sp1_crc504(~0u,p,508);
#else
 static const uint32_t t[16]={0,0x1db71064,0x3b6e20c8,0x26d930ac,0x76dc4190,0x6b6b51f4,0x4db26158,0x5005713c,0xedb88320,0xf00f9344,0xd6d6a3e8,0xcb61b38c,0x9b64c2b0,0x86d3d2d4,0xa00ae278,0xbdbdf21c};
 uint32_t c=~0u;for(unsigned n=0;n<508;n++){c^=p[n];c=(c>>4)^t[c&15];c=(c>>4)^t[c&15];}return ~c;
#endif
}
static void fresh(uint8_t*p,unsigned type){memset(p,0,512);memcpy(p,"PG422IDX",8);put(p+8,type);}
static bool valid(const uint8_t*p,unsigned type){return !memcmp(p,"PG422IDX",8)&&u32(p+8)==type&&u32(p+508)==crc(p);}
static bool page(pg422*s,uint32_t at,unsigned type){
 if(at<2||at>=s->tail)return false;
 for(unsigned i=0;i<5;i++)if(s->cache_at[i]==at){
  if(u32(s->cache[i]+8)!=type)return false;
  memcpy(s->scratch,s->cache[i],512);return true;
 }
 if(!s->io.read(s->io.ctx,at,s->scratch)||!valid(s->scratch,type))return false;
 unsigned slot=type==2?0:type==1?1:2+(s->cache_next++%3);
 memcpy(s->cache[slot],s->scratch,512);s->cache_at[slot]=at;return true;
}
static bool write_page(pg422*s,uint32_t at){
 /* Invalidate BEFORE attempting a write: a failed/partial write cannot be
  * hidden by a previously checked cached private transaction page. */
 for(unsigned i=0;i<5;i++)if(s->cache_at[i]==at)s->cache_at[i]=0;
 put(s->scratch+508,crc(s->scratch));return s->io.write(s->io.ctx,at,s->scratch);
}
static bool verified_page(pg422*s,uint32_t at){
 /* Retain the expected checksum before readback, not merely a self-consistent
  * checksum from the device. The caller holds exclusive storage ownership. */
 uint32_t expected=crc(s->scratch),type=u32(s->scratch+8);
 /*436: transaction-private pages cannot be reached by a durable root.
  * Verify delivery now; pg422_commit flushes ALL data before publishing the
  * root, then root_write flushes/verifies the root separately. Outside a
  * transaction retain the original per-page durability barrier. */
 if(!write_page(s,at)||(!s->transaction&&!s->io.sync(s->io.ctx))||!s->io.read(s->io.ctx,at,s->scratch))return false;
 return valid(s->scratch,type)&&u32(s->scratch+508)==expected;
}
static bool root_write(pg422*s,unsigned slot,uint64_t gen,uint32_t root,uint32_t tail){
 fresh(s->scratch,0);put(s->scratch+12,(uint32_t)gen);put(s->scratch+16,gen>>32);put(s->scratch+20,root);put(s->scratch+24,tail);
 if(!write_page(s,slot)||!s->io.sync(s->io.ctx))return false;
 if(!s->io.read(s->io.ctx,slot,s->scratch)||!valid(s->scratch,0))return false;
 return u32(s->scratch+12)==(uint32_t)gen&&u32(s->scratch+16)==gen>>32&&u32(s->scratch+20)==root&&u32(s->scratch+24)==tail;
}
bool pg422_open(pg422*s,pg422_io io){
 if(!s||!io.read||!io.write||!io.sync||io.sectors<5)return false;
 memset(s,0,sizeof(*s));s->io=io;bool found=false;
 for(unsigned slot=0;slot<2;slot++){
  if(!io.read(io.ctx,slot,s->scratch))return false;
  if(!valid(s->scratch,0))continue;
  uint64_t gen=u32(s->scratch+12)|(uint64_t)u32(s->scratch+16)<<32;
  uint32_t root=u32(s->scratch+20),tail=u32(s->scratch+24);
  if(tail<2||tail>io.sectors||(root&&(root<2||root>=tail)))continue;
  if(found&&gen==s->generation&&(root!=s->root||tail!=s->tail))return false;
  if(!found||gen>s->generation){s->generation=gen;s->root=root;s->tail=tail;s->active=slot;found=true;}
 }
 if(!found)return false;
 /* Don't silently roll back a newer valid root whose page is corrupt. */
 if(s->root&&!page(s,s->root,2))return false;
 s->ready=true;return true;
}
bool pg422_probe(pg422*s,pg422_io io,bool*found,uint64_t*generation){
 if(!s||!found||!generation||!io.read||io.sectors<5)return false;
 *found=false;*generation=0;uint32_t saved_root=0,saved_tail=0;
 for(unsigned i=0;i<2;i++){
  if(!io.read(io.ctx,i,s->scratch))return false;
  if(!valid(s->scratch,0))continue;
  uint64_t gen=u32(s->scratch+12)|(uint64_t)u32(s->scratch+16)<<32;
  uint32_t root=u32(s->scratch+20),tail=u32(s->scratch+24);
  if(tail<2||tail>io.sectors||(root&&(root<2||root>=tail)))return false;
  if(*found&&gen==*generation&&(root!=saved_root||tail!=saved_tail))return false;
  if(!*found||gen>*generation){*found=true;*generation=gen;saved_root=root;saved_tail=tail;}
 }
 return true;
}
bool pg422_create(pg422*s,pg422_io io){
 if(!s||!io.read||!io.write||!io.sync||io.sectors<5)return false;
 memset(s,0,sizeof(*s));s->io=io;
 return root_write(s,0,1,0,2)&&root_write(s,1,1,0,2)&&pg422_open(s,io);
}
bool pg422_get(pg422*s,uint32_t id,uint8_t out[32]){
 if(!s||!s->ready||!out||id>=PG422_LIMIT)return false;
 memset(out,0,32);if(!s->root)return true;
 unsigned leaf=id/15,a=leaf/123,b=leaf%123;
 if(!page(s,s->root,2))return false;
 uint32_t branch=u32(s->scratch+16+a*4);if(!branch)return true;
 if(!page(s,branch,1))return false;
 uint32_t data=u32(s->scratch+16+b*4);if(!data)return true;
 if(!page(s,data,3))return false;
 memcpy(out,s->scratch+28+(id%15)*32,32);return true;
}
bool pg422_begin(pg422*s){if(!s||!s->ready||s->transaction||!s->io.write||!s->io.sync)return false;s->transaction_first=s->tail;s->transaction=true;return true;}
bool pg422_stage(pg422*s,uint32_t id,const uint8_t rec[32]){
 if(!s||!s->ready||!s->transaction||!rec||id>=PG422_LIMIT||s->generation==UINT64_MAX||s->tail>s->io.sectors-3)return false;
 unsigned leaf=id/15,a=leaf/123,b=leaf%123;uint32_t branch=0,data=0,t=s->tail;
 if(s->root){if(!page(s,s->root,2))goto fail;branch=u32(s->scratch+16+a*4);}
 if(branch){if(!page(s,branch,1))goto fail;data=u32(s->scratch+16+b*4);}
 /* Pages allocated since begin are unreachable from both durable roots.
  * Reusing only those private pages avoids repeatedly copying the same path
  * while assembling one atomic multi-record transaction. */
 uint32_t new_data=data>=s->transaction_first?data:t++;
 if(data){if(!page(s,data,3))goto fail;}else fresh(s->scratch,3);
 memcpy(s->scratch+28+(id%15)*32,rec,32);if(!verified_page(s,new_data))goto fail;
 if(new_data==data)return true;
 uint32_t new_branch=branch>=s->transaction_first?branch:t++;
 if(branch){if(!page(s,branch,1))goto fail;}else fresh(s->scratch,1);
 put(s->scratch+16+b*4,new_data);if(!verified_page(s,new_branch))goto fail;
 if(new_branch==branch){s->tail=t;return true;}
 uint32_t new_root=s->root>=s->transaction_first?s->root:t++;
 if(s->root){if(!page(s,s->root,2))goto fail;}else fresh(s->scratch,2);
 put(s->scratch+16+a*4,new_branch);if(!verified_page(s,new_root))goto fail;
 s->root=new_root;s->tail=t;return true;
fail:s->ready=false;return false;
}
bool pg422_commit(pg422*s){
 if(!s||!s->ready||!s->transaction||s->generation==UINT64_MAX)return false;
 if(!s->io.sync(s->io.ctx)||!root_write(s,s->active^1,s->generation+1,s->root,s->tail)){s->ready=false;return false;}
 s->active^=1;s->generation++;s->transaction=false;return true;
}
bool pg422_put(pg422*s,uint32_t id,const uint8_t rec[32]){
 if(!s||!s->ready||s->transaction||!rec||id>=PG422_LIMIT||s->generation==UINT64_MAX||s->tail>s->io.sectors-3)return false;
 return pg422_begin(s)&&pg422_stage(s,id,rec)&&pg422_commit(s);
}

bool pg422_abort(pg422*s){if(!s)return false;pg422_io io=s->io;return pg422_open(s,io);}
/* Offline compaction only, to an exclusively owned, disjoint destination.
 * Keep the original file until a higher-level recovery journal publishes the
 * destination. Copy every allocated leaf including tombstones: no take or
 * payload liveness decision is made here. */
bool pg422_compact(pg422*src,pg422*dst,pg422_io target){
 if(!src||!dst||src==dst||!src->ready||src->transaction)return false;
 /* Same callback/context namespace cannot prove disjointness. */
 if(target.ctx==src->io.ctx)return false;
 if(!pg422_create(dst,target)||!pg422_begin(dst))return false;
 dst->generation=src->generation;
 if(!src->root)return pg422_commit(dst);
 uint32_t tail=2;
 for(unsigned a=0;a<PG422_FANOUT;a++){
  if(!page(src,src->root,2))goto fail;
  uint32_t branch=u32(src->scratch+16+4*a);if(!branch)continue;
  uint32_t first=tail;
  for(unsigned b=0;b<PG422_FANOUT;b++){
   if(!page(src,branch,1))goto fail;
   uint32_t leaf=u32(src->scratch+16+4*b);if(!leaf)continue;
   if(tail>=target.sectors||!page(src,leaf,3))goto fail;
   memcpy(dst->scratch,src->scratch,512);
   if(!verified_page(dst,tail++))goto fail;
  }
  if(tail>=target.sectors||!page(src,branch,1))goto fail;
  memcpy(dst->scratch,src->scratch,512);
  for(unsigned b=0;b<PG422_FANOUT;b++)if(u32(dst->scratch+16+4*b))put(dst->scratch+16+4*b,first++);
  if(first!=tail||!verified_page(dst,tail++))goto fail;
 }
 /* Reconstruct the compact branch positions without a pointer array. */
 fresh(dst->scratch,2);uint32_t next=2;
 for(unsigned a=0;a<PG422_FANOUT;a++){
  if(!page(src,src->root,2))goto fail;
  uint32_t branch=u32(src->scratch+16+4*a);if(!branch)continue;
  if(!page(src,branch,1))goto fail;
  for(unsigned b=0;b<PG422_FANOUT;b++)if(u32(src->scratch+16+4*b))next++;
  put(dst->scratch+16+4*a,next++);
 }
 if(next!=tail||tail>=target.sectors||!verified_page(dst,tail))goto fail;
 dst->root=tail;dst->tail=tail+1;
 return pg422_commit(dst);
fail:dst->ready=false;return false;
}
/* Read-only view of one durable checkpoint for allocator pinning. */
bool pg422_checkpoint(pg422*s,pg422_io io,unsigned slot){
 if(!s||!io.read||slot>1||io.sectors<5)return false;
 memset(s,0,sizeof(*s));s->io=io;s->io.write=0;s->io.sync=0;
 if(!io.read(io.ctx,slot,s->scratch)||!valid(s->scratch,0))return false;
 s->generation=u32(s->scratch+12)|(uint64_t)u32(s->scratch+16)<<32;
 s->root=u32(s->scratch+20);s->tail=u32(s->scratch+24);s->active=slot;
 if(s->tail<2||s->tail>io.sectors||(s->root&&(s->root<2||s->root>=s->tail)))return false;
 if(s->root&&!page(s,s->root,2))return false;s->ready=true;return true;
}
bool pg422_walk(pg422*s,pg422_visit visit,void*ctx){
 if(!s||!s->ready||!visit)return false;if(!s->root)return true;
 /* These two sector buffers live in the already shared storage workspace,
  * not retained RAM or the audio stack. Read each tree page once per walk. */
 if(!page(s,s->root,2))return false;memcpy(s->walk_root,s->scratch,512);
 for(unsigned a=0;a<PG422_FANOUT;a++){
  uint32_t branch=u32(s->walk_root+16+4*a);if(!branch)continue;
  if(!page(s,branch,1))return false;memcpy(s->walk_branch,s->scratch,512);
  for(unsigned b=0;b<PG422_FANOUT;b++){
   uint32_t leaf=u32(s->walk_branch+16+4*b);if(!leaf)continue;
   if(!page(s,leaf,3))return false;
   for(unsigned r=0;r<PG422_RECORDS;r++){const uint8_t*p=s->scratch+28+32*r;bool any=false;for(unsigned i=0;i<32;i++)any|=p[i]!=0;
    if(any&&!visit(ctx,(a*PG422_FANOUT+b)*PG422_RECORDS+r,p))return false;
   }
  }
 }
 return true;
}
/* Explicit coordinator recovery of a torn inactive superblock. Requires a
 * successfully opened checkpoint and no staged transaction. Never formats or
 * guesses through unreadable media or a newer, structurally valid root. */
bool pg422_repair_inactive(pg422*s){
 if(!s||!s->ready||s->transaction||!s->io.write||!s->io.sync)return false;
 unsigned other=s->active^1u;
 if(!s->io.read(s->io.ctx,other,s->scratch))return false;
 if(valid(s->scratch,0))return true;
 if(!root_write(s,other,s->generation,s->root,s->tail)){s->ready=false;return false;}
 return true;
}
