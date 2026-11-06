#include "edit.h"
bool take_edit_begin(pool_store *s,uint16_t source,pool_writer *w){
 return take_edit_begin_capacity(s,source,0,w);
}
bool take_edit_begin_capacity(pool_store *s,uint16_t source,uint32_t capacity,pool_writer *w){
 take_metadata m;pool_object audio;
 if(!take_load(s,source,&m,&audio)||!pool_verify_ref(s,source))return false;
 pool_object old=s->current.object[s->current.ref[source]];
 if(capacity && capacity<m.audio_blocks)return false;
 if(capacity>UINT32_MAX-TAKE_HEADER_BLOCKS)return false;
 uint32_t reserve=capacity?capacity+TAKE_HEADER_BLOCKS:old.blocks;
 if(!pool_writer_room(s,reserve,16,6,w)||!pool_prepare_writer_aligned(s,reserve,16,w))return false;
 for(uint32_t offset=0;offset<old.blocks;){
  uint32_t n=old.blocks-offset;if(n>8)n=8;
  if(!pool_read_ref(s,source,offset,s->bytes,n)||!pool_writer_append(s->io,w,s->bytes,n)){w->failed=true;return false;}
  offset+=n;
 }
 return true;
}
bool take_edit_patch(pool_io io,pool_writer *w,uint32_t offset,const uint8_t *data,uint32_t n,uint8_t scratch[512]){
 if(!w||!w->active||w->failed||!data||!scratch||!io.read||!io.write||!n||n>16||
    w->written<=TAKE_HEADER_BLOCKS||w->written>w->capacity||offset>=w->written-TAKE_HEADER_BLOCKS||
    n>w->written-TAKE_HEADER_BLOCKS-offset||w->first>=io.pool_blocks||
    w->capacity>io.pool_blocks-w->first||io.pool_first>=io.device_blocks||
    w->first>=io.device_blocks-io.pool_first||w->written>io.device_blocks-io.pool_first-w->first)return false;
 offset+=TAKE_HEADER_BLOCKS;
 uint32_t delta=0;
 for(uint32_t i=0;i<n;i++){
  if(!io.read(io.context,io.pool_first+w->first+offset+i,scratch,1)){w->failed=true;return false;}
  for(unsigned b=0;b<512;b++)scratch[b]^=data[i*512+b];
  delta=pool_crc(delta,scratch,512);
 }
 if(!io.write(io.context,io.pool_first+w->first+offset,data,n)){w->failed=true;return false;}
 w->crc^=pool_crc_shift(delta,(uint64_t)(w->written-offset-n)*512);
 return true;
}
