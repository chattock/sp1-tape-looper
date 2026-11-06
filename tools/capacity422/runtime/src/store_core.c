#include "store_core.h"
#include <stddef.h>
#include <limits.h>
bool sp1_extent_valid(sp1_volume v, sp1_extent e) {
 if (!v.count || v.first >= v.device_blocks || v.count > v.device_blocks-v.first)
  return false;
 return e.count && e.first >= v.first && e.first-v.first < v.count &&
        e.count <= v.count-(e.first-v.first);
}
bool sp1_extent_map(sp1_extent e,uint32_t offset,uint32_t requested,
                    uint32_t *physical,uint32_t *count) {
 if(!physical || !count || !requested || !e.count || offset>=e.count ||
    e.count-1>UINT32_MAX-e.first) return false;
 uint32_t remaining=e.count-offset;
 *physical=e.first+offset;
 *count=requested<remaining?requested:remaining;
 return true;
}
bool sp1_extents_disjoint(sp1_volume v,const sp1_extent *e,uint32_t n) {
 /* Validation is maintenance-only, bounded to256 objects in this prototype. */
 if(!e || !n || n>256) return false;
 for(uint32_t i=0;i<n;i++) {
  if(!sp1_extent_valid(v,e[i])) return false;
  for(uint32_t j=0;j<i;j++) {
   uint32_t lo=e[i].first<e[j].first?i:j, hi=lo==i?j:i;
   if(e[hi].first-e[lo].first<e[lo].count) return false;
  }
 }
 return true;
}
/* Called by one storage coordinator, not concurrently from IRQ/audio/USB.
 * The application must serialize requests and revoke/publish descriptors.
 * A state machine is not a substitute for that synchronization. */
void sp1_owner_init(sp1_ownership *s) {
 *s=(sp1_ownership){.owner=SP1_STORE_VALIDATING};
}
bool sp1_owner_validated(sp1_ownership *s,bool valid) {
 if(s->owner!=SP1_STORE_VALIDATING || s->readers || s->writer)return false;
 s->owner=valid?SP1_STORE_PERFORMANCE:SP1_STORE_INVALID; return valid;
}
bool sp1_reader_acquire(sp1_ownership *s) {
 if(s->owner!=SP1_STORE_PERFORMANCE || s->readers==UINT32_MAX)return false;
 ++s->readers;return true;
}
bool sp1_reader_release(sp1_ownership *s) {
 if(!s->readers)return false;
 --s->readers;return true;
}
bool sp1_writer_acquire(sp1_ownership *s) {
 if(s->owner!=SP1_STORE_PERFORMANCE || s->writer)return false;
 s->writer=true;return true;
}
bool sp1_writer_release(sp1_ownership *s) {
 if(!s->writer)return false;
 s->writer=false;return true;
}
bool sp1_host_request(sp1_ownership *s,bool stopped) {
 if(!stopped || s->owner!=SP1_STORE_PERFORMANCE)return false;
 s->owner=SP1_STORE_DRAINING;return true;
}
bool sp1_host_grant(sp1_ownership *s,bool flushed,bool closed,bool drained) {
 if(s->owner!=SP1_STORE_DRAINING || s->readers || s->writer || !flushed ||
    !closed || !drained)return false;
 s->owner=SP1_STORE_HOST;return true;
}
bool sp1_host_return(sp1_ownership *s) {
 if(s->owner!=SP1_STORE_HOST)return false;
 /* Both normal eject and unexpected disconnect revoke all prior mappings.
  * Successful revalidation is required before any performance access. */
 s->owner=SP1_STORE_VALIDATING;return true;
}

bool sp1_fat_contiguous(sp1_volume data,uint32_t spc,uint32_t clusters,
                       uint32_t first,uint32_t reserved,sp1_fat_read read,
                       void *context,sp1_extent *out) {
 if(!read || !out || !spc || spc>128 || (spc&(spc-1)) || !clusters ||
    clusters>0x0fffffee || first<2 || first-2>=clusters || !reserved ||
    reserved>clusters-(first-2) || clusters>data.count/spc) return false;
 uint64_t start=(uint64_t)data.first+(uint64_t)(first-2)*spc;
 uint64_t count=(uint64_t)reserved*spc;
 if(start>UINT32_MAX || count>UINT32_MAX) return false;
 sp1_extent result={(uint32_t)start,(uint32_t)count};
 if(!sp1_extent_valid(data,result))return false;
 for(uint32_t i=0;i<reserved;i++) {
  uint32_t entry;
  if(!read(context,first+i,&entry))return false;
  entry&=0x0fffffff;
  if(i+1==reserved) {if(entry<0x0ffffff8)return false;}
  else if(entry!=first+i+1)return false;
 }
 *out=result; return true;
}

/* Bound a streaming read at the musical loop boundary independently of the
 * physical payload reservation. Reverse reads still use an ascending device
 * range; the decoder consumes the returned blocks in reverse order. */
bool sp1_extent_window(sp1_extent e,uint32_t loop_blocks,uint32_t offset,
                       uint32_t wanted,bool reverse,uint32_t *lba,uint32_t *count) {
 if(!loop_blocks||loop_blocks>e.count||!wanted||!lba||!count)return false;
 uint32_t position=offset%loop_blocks;
 uint32_t available=reverse?position+1:loop_blocks-position;
 uint32_t n=wanted<available?wanted:available;
 return sp1_extent_map(e,reverse?position-(n-1):position,n,lba,count);
}
