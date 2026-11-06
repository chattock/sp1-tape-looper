#include "session.h"
#include "edit.h"
#include <string.h>
static bool quiet(music_session *m,bool stopped){return m&&stopped&&m->ownership.owner==SP1_STORE_PERFORMANCE&&!m->ownership.readers&&!m->ownership.writer;}
static void invalidate(music_session *m){memset(m->tracks,0,sizeof(m->tracks));m->ownership.owner=SP1_STORE_INVALID;}
void music_session_init(music_session *m){memset(m,0,sizeof(*m));sp1_owner_init(&m->ownership);}
bool music_session_load_opened(music_session *m,pool_store *s,unsigned song,take_metadata out[4]){
 if(!m||!s||!out||song>=16||m->ownership.readers||m->ownership.writer||
    (m->ownership.owner!=SP1_STORE_VALIDATING&&m->ownership.owner!=SP1_STORE_PERFORMANCE))return false;
 sp1_extent pending[4]={{0}};memset(out,0,4*sizeof(*out));
 if(!s->ready){invalidate(m);return false;}
 for(unsigned i=0;i<4;i++){
  unsigned ref=song*4+i;if(s->current.ref[ref]==SP1_POOL_NONE)continue;
  pool_object a;if(!take_load(s,ref,&out[i],&a)){invalidate(m);return false;}
  pending[i]=(sp1_extent){a.first,a.blocks};
 }
 memcpy(m->tracks,pending,sizeof(pending));m->song=(uint16_t)song;m->ownership.owner=SP1_STORE_PERFORMANCE;return true;
}
bool music_session_load(music_session *m,pool_store *s,pool_io io,unsigned song,take_metadata out[4]){
 if(!m||!s||!out||song>=16||m->ownership.readers||m->ownership.writer||
    (m->ownership.owner!=SP1_STORE_VALIDATING&&m->ownership.owner!=SP1_STORE_PERFORMANCE))return false;
 if(!pool_open(s,io)){memset(out,0,4*sizeof(*out));invalidate(m);return false;}
 return music_session_load_opened(m,s,song,out);
}
bool music_session_map(const music_session *m,unsigned track,uint32_t offset,uint32_t wanted,uint32_t *lba,uint32_t *count){
 return m&&m->ownership.owner==SP1_STORE_PERFORMANCE&&track<4&&sp1_extent_map(m->tracks[track],offset,wanted,lba,count);
}
bool music_session_stash(music_session *m,pool_store *s,pool_io io,unsigned track,unsigned stash,bool stopped){
 if(!quiet(m,stopped)||track>=4||stash>=64)return false;
 if(!pool_open(s,io)){invalidate(m);return false;}
 if(!pool_stash(s,m->song*4+track,stash)){if(!s->ready)invalidate(m);return false;}
 m->tracks[track]=(sp1_extent){0};return true;
}
bool music_session_recall(music_session *m,pool_store *s,pool_io io,unsigned stash,unsigned track,bool stopped){
 if(!quiet(m,stopped)||track>=4||stash>=64)return false;
 if(!pool_open(s,io)){invalidate(m);return false;}
 /* Validate before publication, so malformed objects cannot be recalled. */
 take_metadata meta;pool_object a;
 if(!take_load(s,64+stash,&meta,&a))return false;
 if(!pool_recall(s,stash,m->song*4+track)){if(!s->ready)invalidate(m);return false;}
 m->tracks[track]=(sp1_extent){a.first,a.blocks};return true;
}
bool music_session_record(music_session *m,pool_store *s,pool_io io,unsigned track,uint32_t capacity,bool stopped){
 if(!quiet(m,stopped)||track>=4)return false;
 if(!pool_open(s,io)){invalidate(m);return false;}
 if(!take_begin(s,capacity,&m->writer))return false;
 m->destination=m->song*4+track;__atomic_store_n(&m->writer_phase,2,__ATOMIC_RELEASE);return sp1_writer_acquire(&m->ownership);
}
bool music_session_append(music_session *m,pool_io io,const void *data,uint32_t blocks){
 return m&&m->ownership.owner==SP1_STORE_PERFORMANCE&&m->ownership.writer&&m->writer_phase==2&&pool_writer_append(io,&m->writer,data,blocks);
}
bool music_session_finish(music_session *m,pool_store *s,pool_io io,const take_metadata *meta,bool stopped){
 if(!m||!stopped||m->ownership.owner!=SP1_STORE_PERFORMANCE||!m->ownership.writer||m->writer_phase!=2||m->ownership.readers)return false;
 if(!pool_open(s,io)||!take_finish(s,&m->writer,m->destination,meta,true)){invalidate(m);return false;}
 m->tracks[m->destination%4]=(sp1_extent){io.pool_first+m->writer.first+TAKE_HEADER_BLOCKS,meta->audio_blocks};
 __atomic_store_n(&m->writer_phase,0,__ATOMIC_RELEASE);return sp1_writer_release(&m->ownership);
}
bool music_session_abort(music_session *m,bool stopped){
 if(!m||!stopped||m->ownership.readers||!m->ownership.writer||m->writer_phase==MUSIC_AUDITION427)return false;
 memset(&m->writer,0,sizeof(m->writer));__atomic_store_n(&m->writer_phase,0,__ATOMIC_RELEASE);return sp1_writer_release(&m->ownership);
}
bool music_session_host_request(music_session *m,bool stopped){
 /* Refuse while recording rather than stranding a writer in DRAINING. */
 if(m)music_prepared427_revoke(m);
 return m&&!m->ownership.writer&&sp1_host_request(&m->ownership,stopped);
}
bool music_session_host_grant(music_session *m,bool synced,bool closed,bool drained){
 if(!m||!sp1_host_grant(&m->ownership,synced,closed,drained))return false;
 memset(m->tracks,0,sizeof(m->tracks));return true;
}
bool music_session_host_return(music_session *m){return m&&sp1_host_return(&m->ownership);}

bool music_session_stash_control(music_session *m,pool_store *s,pool_io io,unsigned track,unsigned stash,bool stopped,music_control_snapshot snapshot,void *ctx){
 if(!quiet(m,stopped)||!s||!snapshot||track>=4||stash>=64)return false;
 if(!pool_open(s,io)){invalidate(m);return false;}
 if(!pool_stage_stash(s,m->song*4+track,stash))return false;
 if(!music_control_save(s,snapshot,ctx)){invalidate(m);return false;}
 m->tracks[track]=(sp1_extent){0};return true;
}
bool music_session_recall_control(music_session *m,pool_store *s,pool_io io,unsigned stash,unsigned track,bool stopped,music_control_snapshot snapshot,void *ctx,take_metadata *out){
 if(!quiet(m,stopped)||!s||!snapshot||!out||track>=4||stash>=64)return false;
 if(!pool_open(s,io)){invalidate(m);return false;}
 take_metadata meta;pool_object audio;
 if(!take_load(s,64+stash,&meta,&audio)||!pool_stage_recall(s,stash,m->song*4+track))return false;
 if(!music_control_save(s,snapshot,ctx)){invalidate(m);return false;}
 m->tracks[track]=(sp1_extent){audio.first,audio.blocks};*out=meta;return true;
}
bool music_session_finish_control(music_session *m,pool_store *s,pool_io io,const take_metadata *meta,bool stopped,music_control_snapshot snapshot,void *ctx){
 if(!m||!s||!meta||!snapshot||!stopped||m->ownership.owner!=SP1_STORE_PERFORMANCE||!m->ownership.writer||m->writer_phase!=2||m->ownership.readers)return false;
 if(!pool_open(s,io)||!take_stage(s,&m->writer,m->destination,meta,true)||!music_control_save(s,snapshot,ctx)){invalidate(m);return false;}
 m->tracks[m->destination%4]=(sp1_extent){io.pool_first+m->writer.first+TAKE_HEADER_BLOCKS,meta->audio_blocks};
 __atomic_store_n(&m->writer_phase,0,__ATOMIC_RELEASE);return sp1_writer_release(&m->ownership);
}
bool music_session_edit(music_session *m,pool_store *s,pool_io io,unsigned track,bool stopped){
 return music_session_edit_capacity(m,s,io,track,0,stopped);
}
bool music_session_edit_capacity(music_session *m,pool_store *s,pool_io io,unsigned track,uint32_t capacity,bool stopped){
 if(!quiet(m,stopped)||!s||track>=4)return false;
 if(!pool_open(s,io)){invalidate(m);return false;}
 if(!take_edit_begin_capacity(s,m->song*4+track,capacity,&m->writer))return false;
 m->destination=m->song*4+track;__atomic_store_n(&m->writer_phase,2,__ATOMIC_RELEASE);return sp1_writer_acquire(&m->ownership);
}
bool music_session_patch(music_session *m,pool_io io,uint32_t offset,const uint8_t *data,uint32_t blocks,uint8_t scratch[512]){
 return m&&m->ownership.owner==SP1_STORE_PERFORMANCE&&m->ownership.writer&&m->writer_phase==2&&take_edit_patch(io,&m->writer,offset,data,blocks,scratch);
}

bool music_session_write(music_session *m,pool_io io,uint32_t offset,const uint8_t *data,uint32_t blocks,uint8_t scratch[512]){
 if(!m||m->ownership.owner!=SP1_STORE_PERFORMANCE||!m->ownership.writer||m->writer_phase!=2||!data||!scratch||!blocks||blocks>16||
    !m->writer.active||m->writer.failed||m->writer.written<TAKE_HEADER_BLOCKS||m->writer.written>m->writer.capacity)return false;
 uint32_t written=m->writer.written-TAKE_HEADER_BLOCKS;
 if(offset>written||offset>m->writer.capacity-TAKE_HEADER_BLOCKS||blocks>m->writer.capacity-TAKE_HEADER_BLOCKS-offset)return false;
 uint32_t patch=written-offset;if(patch>blocks)patch=blocks;
 if(patch&&!take_edit_patch(io,&m->writer,offset,data,patch,scratch))return false;
 if(blocks>patch&&!pool_writer_append(io,&m->writer,data+patch*512,blocks-patch)){m->writer.failed=true;return false;}
 return true;
}

bool music_session_edit_prepare(music_session *m,pool_store *s,pool_io io,unsigned track,uint32_t capacity,bool stopped){
 if(!quiet(m,stopped)||!s||track>=4)return false;
 if(!pool_open(s,io)){invalidate(m);return false;}
 take_metadata meta;pool_object audio;
 if(!take_load(s,m->song*4+track,&meta,&audio)||!audio.blocks||audio.first<TAKE_HEADER_BLOCKS||
    audio.first!=m->tracks[track].first||audio.blocks!=m->tracks[track].count)return false;
 if(!capacity)capacity=audio.blocks;
 if(capacity<audio.blocks||capacity>UINT32_MAX-TAKE_HEADER_BLOCKS||
    !pool_writer_room(s,capacity+TAKE_HEADER_BLOCKS,16,6,&m->writer)||
    !pool_prepare_writer_aligned(s,capacity+TAKE_HEADER_BLOCKS,16,&m->writer))return false;
 m->destination=m->song*4+track;__atomic_store_n(&m->writer_phase,1,__ATOMIC_RELEASE);
 return sp1_writer_acquire(&m->ownership);
}
bool music_session_edit_step(music_session *m,pool_io io,void *scratch,uint32_t n){
 if(!m||!scratch||!n||n>8||m->writer_phase!=1||!m->ownership.writer||
    m->ownership.owner!=SP1_STORE_PERFORMANCE||m->ownership.readers||m->writer.failed||m->destination>=64)return false;
 sp1_extent source=m->tracks[m->destination%4];
 if(source.first<TAKE_HEADER_BLOCKS||source.count>UINT32_MAX-TAKE_HEADER_BLOCKS)return false;
 uint32_t length=source.count+TAKE_HEADER_BLOCKS;
 if(m->writer.written>=length)return false;
 if(n>length-m->writer.written)n=length-m->writer.written;
 if(!io.read||!io.read(io.context,source.first-TAKE_HEADER_BLOCKS+m->writer.written,scratch,n)){
  m->writer.failed=true;return false;
 }
 return pool_writer_append(io,&m->writer,scratch,n);
}
bool music_session_edit_ready(music_session *m,pool_store *s,pool_io io){
 if(!m||!s||m->writer_phase!=1||!m->ownership.writer||m->ownership.owner!=SP1_STORE_PERFORMANCE||
    m->ownership.readers||m->writer.failed||m->destination>=64)return false;
 sp1_extent source=m->tracks[m->destination%4];
 if(source.count>UINT32_MAX-TAKE_HEADER_BLOCKS||m->writer.written!=source.count+TAKE_HEADER_BLOCKS)return false;
 if(!pool_open(s,io)||s->current.generation!=m->writer.generation){m->writer.failed=true;return false;}
 unsigned id=s->current.ref[m->destination];
 if(id==SP1_POOL_NONE){m->writer.failed=true;return false;}
 pool_object old=s->current.object[id];
 if(old.blocks!=m->writer.written||io.pool_first+old.first+TAKE_HEADER_BLOCKS!=source.first||old.crc!=~m->writer.crc){m->writer.failed=true;return false;}
 __atomic_store_n(&m->writer_phase,2,__ATOMIC_RELEASE);return true;
}

/*427: RAM-map publication only; does not claim durable storage or release the
 * detached writer. Validate the complete bounded extent before any reader. */
bool music_audition427(music_session*s,pool_io io,unsigned track,const take_metadata*m){
 if(!s||!m||track>=4||s->song>=16||s->destination!=s->song*4+track||
    s->ownership.owner!=SP1_STORE_PERFORMANCE||!s->ownership.writer||s->ownership.readers||
    s->writer_phase!=2||!s->writer.active||s->writer.failed||!take_valid(m)||
    s->writer.written<=TAKE_HEADER_BLOCKS||s->writer.written>s->writer.capacity||
    m->audio_blocks!=s->writer.written-TAKE_HEADER_BLOCKS||
    s->writer.first>=io.pool_blocks||s->writer.written>io.pool_blocks-s->writer.first)return false;
 sp1_extent e={io.pool_first+s->writer.first+TAKE_HEADER_BLOCKS,m->audio_blocks};
 if(!sp1_extent_valid((sp1_volume){.device_blocks=io.device_blocks,.first=io.pool_first,.count=io.pool_blocks},e))return false;
 s->tracks[track]=e;__atomic_store_n(&s->writer_phase,MUSIC_AUDITION427,__ATOMIC_RELEASE);return true;
}
