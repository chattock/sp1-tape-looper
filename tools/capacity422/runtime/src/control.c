#include "control.h"
bool music_control_save(pool_store *s,music_control_snapshot snapshot,void *ctx){
 if(!s||!s->ready||!snapshot)return false;
 pool_writer writer;if(!pool_prepare_writer(s,MUSIC_CONTROL_BLOCKS,&writer)){s->ready=false;return false;}
 for(unsigned i=0;i<MUSIC_CONTROL_BLOCKS;i++){
  if(!snapshot(ctx,i,s->bytes)||!pool_writer_append(s->io,&writer,s->bytes,1)){s->ready=false;return false;}
 }
 return pool_finish_control(s,&writer,MUSIC_CONTROL_TAG);
}
bool music_control_read(pool_store *s,unsigned sector,uint8_t out[512]){
 if(!s||!s->ready||!s->current.control||sector>=MUSIC_CONTROL_BLOCKS)return false;
 pool_object o=s->current.object[s->current.control-1];
 return o.tag==MUSIC_CONTROL_TAG&&o.blocks==MUSIC_CONTROL_BLOCKS&&pool_read_control(s,sector,out,1);
}
bool music_control_verify(pool_store *s){
 if(!s)return false;
 uint32_t crc=UINT32_MAX;
 for(unsigned i=0;i<MUSIC_CONTROL_BLOCKS;i++){
  if(!music_control_read(s,i,s->bytes))return false;
  crc=pool_crc(crc,s->bytes,512);
 }
 return ~crc==s->current.object[s->current.control-1].crc;
}
bool music_control_load(pool_store *s,uint8_t out[MUSIC_CONTROL_BLOCKS*512]){
 if(!s||!out)return false;
 uint32_t crc=UINT32_MAX;
 for(unsigned i=0;i<MUSIC_CONTROL_BLOCKS;i++){
  uint8_t *sector=out+i*512;
  if(!music_control_read(s,i,sector))return false;
  crc=pool_crc(crc,sector,512);
 }
 return ~crc==s->current.object[s->current.control-1].crc;
}
