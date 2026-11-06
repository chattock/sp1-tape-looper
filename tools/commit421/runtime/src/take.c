#include "take.h"
#include <string.h>
#include <limits.h>
static uint32_t get32(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(uint8_t *p,uint32_t x){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(x>>(8*i));}
bool take_valid(const take_metadata *m){
 if(!m || m->rate!=48000 || (m->format!=1&&m->format!=2) ||
    m->frames_per_block!=(m->format==1?280u:496u) || !m->audio_blocks ||
    !m->frames || m->loop_blocks>m->audio_blocks ||
    (!m->loop_blocks && m->frames>(uint64_t)m->audio_blocks*m->frames_per_block) ||
    m->content_blocks>m->audio_blocks || m->track_head_blocks>m->audio_blocks ||
    m->track_tail_blocks>m->audio_blocks-m->track_head_blocks ||
    (m->track_head_blocks+m->track_tail_blocks==m->audio_blocks) ||
    m->song_head_frames>m->song_frames || m->song_tail_frames>m->song_frames-m->song_head_frames ||
    (m->song_frames && m->grid_origin>=m->song_frames) ||
    m->speed_q16>131072 || m->bpm_q8>UINT16_MAX || m->grid_beats>255 ||
    (m->beats_per_bar!=0&&m->beats_per_bar!=3&&m->beats_per_bar!=4&&m->beats_per_bar!=5&&m->beats_per_bar!=7) ||
    m->pan>255 || m->muted>1 || m->chop_div>64 || m->chop_offset>63 ||
    m->nudge>255 || (m->take_preset!=0&&m->take_preset!=1&&m->take_preset!=2&&m->take_preset!=4&&m->take_preset!=8) ||
    m->grid_offset_encoded>255 || m->magnet_cells>256 ||m->content_flags>7||m->song_mode>2)return false;
 if(!m->magnet_cells){for(unsigned i=0;i<32;i++)if(m->magnet[i])return false;}
 for(unsigned i=m->magnet_cells;i<256;i++)if(m->magnet[i/8]&(1u<<(i%8)))return false;
 return true;
}
bool take_encode(const take_metadata *m,uint8_t sector[512]){
 if(!take_valid(m)||!sector)return false;
 memset(sector,0,512);memcpy(sector,"SP1TAKE1",8);put32(sector+8,2);unsigned offset=12;
#define ENCODE(name) put32(sector+offset,m->name);offset+=4;
 TAKE_FIELDS(ENCODE)
#undef ENCODE
 memcpy(sector+128,m->magnet,32);put32(sector+508,~pool_crc(UINT32_MAX,sector,508));return true;
}
bool take_decode(const uint8_t sector[512],take_metadata *m){
 if(!sector||!m||memcmp(sector,"SP1TAKE1",8)||(get32(sector+8)!=1&&get32(sector+8)!=2)||
    get32(sector+508)!=~pool_crc(UINT32_MAX,sector,508))return false;
 if(get32(sector+8)==1&&get32(sector+124))return false;
 for(unsigned i=160;i<508;i++)if(sector[i])return false;
 take_metadata parsed={0};unsigned offset=12;
#define DECODE(name) parsed.name=get32(sector+offset);offset+=4;
 TAKE_FIELDS(DECODE)
#undef DECODE
 memcpy(parsed.magnet,sector+128,32);if(!take_valid(&parsed))return false;*m=parsed;return true;
}
bool take_begin(pool_store *s,uint32_t capacity,pool_writer *writer){
 if(!s||!capacity||capacity>UINT32_MAX-TAKE_HEADER_BLOCKS||
    !pool_record_room(s,capacity+TAKE_HEADER_BLOCKS,16,6)||
    !pool_prepare_writer_aligned(s,capacity+TAKE_HEADER_BLOCKS,16,writer))return false;
 /* Invalid zero prefix until finish: abandoned recordings are never published.
  * It is not necessary to clear all reserved audio or write future FAT metadata. */
 memset(s->bytes,0,TAKE_HEADER_BLOCKS*512 > sizeof(s->bytes)?sizeof(s->bytes):TAKE_HEADER_BLOCKS*512);
 for(unsigned i=0;i<TAKE_HEADER_BLOCKS;i++)if(!pool_writer_append(s->io,writer,s->bytes,1))return false;
 return true;
}
static bool finalize(pool_store *s,pool_writer *w,uint16_t destination,const take_metadata *meta,bool replace,bool publish){
 if(!s||!w||!s->ready||!w->active||w->failed||w->generation!=s->current.generation||
    !take_valid(meta)||w->written<TAKE_HEADER_BLOCKS||
    meta->audio_blocks!=w->written-TAKE_HEADER_BLOCKS||!take_encode(meta,s->bytes))return false;
 /* CRC is linear (also for a cloned take with an existing header): shift the CRC of the
  * changed 512 bytes across the unchanged suffix and XOR into the running CRC.
  * No audio reread, extra capture state, or length-dependent disk work. */
 uint8_t previous[512];
 if(!s->io.read(s->io.context,s->io.pool_first+w->first,previous,1)){w->failed=true;s->ready=false;return false;}
 for(unsigned i=0;i<512;i++)previous[i]^=s->bytes[i];
 uint32_t delta=pool_crc(0,previous,512);
 delta=pool_crc_shift(delta,(uint64_t)(w->written-1)*512);
 if(!s->io.write(s->io.context,s->io.pool_first+w->first,s->bytes,1)||!s->io.sync(s->io.context)){
  w->failed=true;s->ready=false;return false;
 }
 w->crc^=delta;
 return publish?pool_finish_writer(s,w,destination,TAKE_TAG,replace):pool_stage_writer(s,w,destination,TAKE_TAG,replace);
}
static bool same_audio(const take_metadata *a,const take_metadata *b){
 return a->audio_blocks==b->audio_blocks&&a->format==b->format&&a->frames==b->frames&&
  a->start_frames==b->start_frames&&a->loop_blocks==b->loop_blocks&&a->content_blocks==b->content_blocks;
}
bool take_load(pool_store *s,uint16_t ref,take_metadata *meta,pool_object *audio){
 if(!s||!meta||!audio||!s->ready||ref>=SP1_POOL_REFS)return false;
 uint16_t id=s->current.ref[ref];if(id==SP1_POOL_NONE)return false;
 pool_object obj=s->current.object[id];
 if(obj.tag!=TAKE_TAG||obj.blocks<=TAKE_HEADER_BLOCKS||!pool_read_ref(s,ref,0,s->bytes,1)||
    !take_decode(s->bytes,meta)||meta->audio_blocks!=obj.blocks-TAKE_HEADER_BLOCKS)return false;
 if(s->current.annotation[ref]){
  take_metadata base=*meta;
  pool_object annotation=s->current.object[s->current.annotation[ref]-1];
  if(annotation.tag!=TAKE_TAG||!pool_read_annotation(s,ref,s->bytes)||!take_decode(s->bytes,meta)||!same_audio(meta,&base))return false;
 }
 *audio=(pool_object){s->io.pool_first+obj.first+TAKE_HEADER_BLOCKS,meta->audio_blocks,0,meta->format};
 return true;
}

bool take_finish(pool_store *s,pool_writer *w,uint16_t dest,const take_metadata *m,bool replace){return finalize(s,w,dest,m,replace,true);}
bool take_stage(pool_store *s,pool_writer *w,uint16_t dest,const take_metadata *m,bool replace){return finalize(s,w,dest,m,replace,false);}

bool take_annotate(pool_store *s,uint16_t ref,const take_metadata *meta){
 if(!s||!take_valid(meta)||ref>=SP1_POOL_REFS||s->current.ref[ref]==SP1_POOL_NONE)return false;
 take_metadata original;pool_object audio;
 if(!take_load(s,ref,&original,&audio)||!same_audio(meta,&original))return false;
 pool_writer w;
 if(!pool_record_room(s,1,1,6)||!pool_prepare_writer(s,1,&w)||!take_encode(meta,s->bytes)||!pool_writer_append(s->io,&w,s->bytes,1))return false;
 return pool_stage_annotation(s,&w,ref,TAKE_TAG);
}
