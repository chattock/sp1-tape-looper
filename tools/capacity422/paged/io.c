#include "io.h"
#include <limits.h>
#include <string.h>
static bool rd(void*c,uint32_t at,void*b){cat422_device*d=c;return at<d->index_blocks&&d->raw.read(d->raw.context,d->index_first+at,b,1);}
static bool wr(void*c,uint32_t at,const void*b){cat422_device*d=c;return at<d->index_blocks&&d->raw.write(d->raw.context,d->index_first+at,b,1);}
static bool sync(void*c){cat422_device*d=c;return d->raw.sync(d->raw.context);}
static pg422_io index_io(cat422_device*d){return (pg422_io){d,rd,wr,sync,d->index_blocks};}
bool cat422_banks_open(pg422*s,pg422*scratch,cat422_device*d,pool_io raw){
 if(!s||!scratch||s==scratch||!d||!raw.read||!raw.write||!raw.sync||raw.catalog[0]>=raw.device_blocks||raw.catalog[1]>=raw.device_blocks||8192>raw.device_blocks-raw.catalog[0]||8192>raw.device_blocks-raw.catalog[1])return false;
 if((uint64_t)raw.catalog[0]<(uint64_t)raw.catalog[1]+8192&&(uint64_t)raw.catalog[1]<(uint64_t)raw.catalog[0]+8192)return false;
 bool valid[2];uint64_t generation[2];
 *d=(cat422_device){raw,raw.catalog[0],8192};
 for(unsigned i=0;i<2;i++){d->index_first=raw.catalog[i];if(!pg422_probe(scratch,index_io(d),valid+i,generation+i))return false;}
 if(!valid[0]&&!valid[1])return false;
 unsigned chosen=!valid[0]||(valid[1]&&generation[1]>generation[0]);d->index_first=raw.catalog[chosen];
 if(!cat422_device_open(s,d))return false;
 if(valid[0]&&valid[1]&&generation[0]==generation[1]){
  /* Equal generations occur after migration. Both must be byte-identical;
   * never choose arbitrarily between conflicting publications. */
  for(uint32_t at=0;at<s->tail;at++)if(!raw.read(raw.context,raw.catalog[0]+at,s->scratch,1)||!raw.read(raw.context,raw.catalog[1]+at,scratch->scratch,1)||memcmp(s->scratch,scratch->scratch,512))return false;
 }
 return true;
}
bool cat422_banks_compact(pg422*s,pg422*scratch,cat422_device*d,bool idle){
 if(!idle||!s||!scratch||!d||!s->ready||s->transaction||s->generation<2)return false;
 cat422_device target=*d;target.index_first=d->raw.catalog[d->index_first==d->raw.catalog[0]?1:0];
 if(target.index_first==d->index_first||!pg422_compact(s,scratch,index_io(&target)))return false;
 /* Reopen into the persistent workspace context, never retain target's stack
  * address. Normal boot selects the same verified higher generation. */
 d->index_first=target.index_first;return cat422_device_open(s,d);
}
bool cat422_device_open(pg422*s,cat422_device*d){
 if(!d||!d->raw.read||!d->raw.write||!d->raw.sync||d->index_blocks<5||d->index_first>=d->raw.device_blocks||d->index_blocks>d->raw.device_blocks-d->index_first||!d->raw.pool_blocks||d->raw.pool_first>=d->raw.device_blocks||d->raw.pool_blocks>d->raw.device_blocks-d->raw.pool_first)return false;
 if(d->index_first<d->raw.pool_first+d->raw.pool_blocks&&d->raw.pool_first<d->index_first+d->index_blocks)return false;
 if(!pg422_open(s,(pg422_io){d,rd,wr,sync,d->index_blocks}))return false;
 uint8_t h[32];if(!pg422_get(s,0,h))return false;uint32_t blocks=h[16]|(uint32_t)h[17]<<8|(uint32_t)h[18]<<16|(uint32_t)h[19]<<24;
 if(blocks!=d->raw.pool_blocks){s->ready=false;return false;}return true;
}
bool cat426_writer_prepare_hint(pg422*s,pg422*scratch,cat422_device*d,uint32_t capacity,uint32_t hint,pool_writer*w){
 if(!s||!d||!w||!s->ready||s->transaction||!capacity)return false;
 uint32_t first,control;
 /* Reserve enough index append pages for the bounded normal finalization.
  * This conservative margin is not a compaction substitute. */
 if(s->io.sectors<96||s->tail>s->io.sectors-96||!cat426_find_room_hint(s,scratch,capacity,16,d->raw.pool_first,0,0,hint,&first)||!cat422_find_room(s,scratch,6,1,d->raw.pool_first,first,capacity,&control))return false;
 *w=(pool_writer){.generation=s->generation,.first=first,.capacity=capacity,.crc=UINT32_MAX,.active=true};return true;
}
bool cat422_writer_prepare(pg422*s,pg422*scratch,cat422_device*d,uint32_t capacity,pool_writer*w){return cat426_writer_prepare_hint(s,scratch,d,capacity,0,w);}
bool cat422_writer_object(pg422*s,cat422_device*d,pool_writer*w,uint32_t tag,uint32_t*id){
 if(!s||!d||!w||!id||!s->ready||!s->transaction||!w->active||w->failed||!w->written||w->written>w->capacity||w->generation!=s->generation||w->first>=d->raw.pool_blocks||w->written>d->raw.pool_blocks-w->first)return false;
 if(!d->raw.sync(d->raw.context)){s->ready=false;w->failed=true;return false;}
 if(!cat422_add_extent(s,(cat422_extent){w->first,w->written,~w->crc,tag},id)){w->failed=true;return false;}
 w->active=false;return true;
}
bool cat422_writer_stage(pg422*s,cat422_device*d,pool_writer*w,unsigned lane,uint32_t tag){
 uint32_t id;if(lane>=64||!cat422_writer_object(s,d,w,tag,&id))return false;
 if(!cat422_recorded(s,lane,id,0)){w->failed=true;return false;}return true;
}
bool cat422_read_audio(pg422*s,cat422_device*d,unsigned lane,uint32_t offset,void*out,uint32_t blocks){
 cat422_lane l;cat422_extent e;if(!d||!out||!blocks||!cat422_lane_read(s,lane,&l)||!l.audio||!cat422_extent_read(s,l.audio,&e)||offset>=e.blocks||blocks>e.blocks-offset||e.first>=d->raw.pool_blocks||e.blocks>d->raw.pool_blocks-e.first)return false;
 return d->raw.read(d->raw.context,d->raw.pool_first+e.first+offset,out,blocks);
}
