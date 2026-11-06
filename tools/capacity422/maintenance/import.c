#include "import.h"
#include "../../native593/native.h"
#include "../../library-writeback/pack.h"
#include "../../library-writeback/overlay.h"
#include "../../commit421/runtime/src/controls.h"
#include "../../commit421/runtime/src/snapshot.h"
#include <string.h>
#include <limits.h>
static uint32_t get(const uint8_t*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static bool fail(cat422_workspace*w){if(w)w->index.ready=false;return false;}
static bool target(cat422_workspace*w,const import422_edit*e,cat422_lane*l){
 if(e->lane>=65||!cat422_lane_read(&w->index,e->lane,l))return false;
 if(e->action==IMPORT422_ADD)return !e->archive&&!e->audio&&!e->annotation;
 if(!e->audio)return false;
 if(!e->archive)return e->lane<64&&!l->selected&&l->audio==e->audio&&l->annotation==e->annotation;
 uint8_t b[32];uint32_t id=l->head,previous=0;
 for(uint32_t n=0;n<l->count&&n<PG422_LIMIT;n++){
  if(!id||!pg422_get(&w->index,id,b)||get(b)!=4||get(b+12)!=e->lane||get(b+16)!=previous)return false;
  if(id==e->archive)return get(b+4)==e->audio&&get(b+8)==e->annotation;
  previous=id;id=get(b+20);
 }return false;
}
static bool verify(cat422_workspace*w,cat422_device*d,uint32_t id,uint8_t*buffer,unsigned batch){
 cat422_extent e;if(!cat422_extent_read(&w->index,id,&e))return false;uint32_t crc=UINT32_MAX;
 for(uint32_t off=0;off<e.blocks;){unsigned n=e.blocks-off;if(n>batch)n=batch;if(!d->raw.read(d->raw.context,d->raw.pool_first+e.first+off,buffer,n))return false;crc=pool_crc(crc,buffer,n*512);off+=n;}return ~crc==e.crc;
}
#ifdef IMPORT469
__attribute__((section(".conv469.text.import504")))
#endif
bool import593_stage(cat422_workspace*w,cat422_device*d,const import422_edit*e,uint32_t*hint,uint32_t reserved_end,unsigned*input_error){
 if(input_error)*input_error=0;
 if(!w||!d||!e||!w->index.ready||!w->index.transaction||e->action<1||e->action>3)return false;
 cat422_lane before;if(!target(w,e,&before))return fail(w);
 bool current=e->lane<64&&(e->archive?before.selected==e->archive:e->action!=IMPORT422_ADD);
 if(e->action==IMPORT422_DELETE){
  if(!(e->archive?cat422_archive_delete(&w->index,e->lane,e->archive):cat422_delete(&w->index,e->lane)))return fail(w);
  if(current&&!music_controls_track(w->controls,e->lane/4,e->lane%4,0))return fail(w);return true;
 }
 wav_info wav={0};native593_info native;
 bool prepared=native593_probe(e->source,&native,w->sector);
 if(prepared){if(native.lane!=e->lane||native.action!=e->action)return fail(w);wav.frames=native.frames;wav.rate=48000;wav.format=1;wav.bits=16;}
 else if(wav_inspect(e->source,&wav)!=WAV_OK)return fail(w);
 uint32_t blocks=(wav.frames+279ull)/280,first;
 if(!blocks||blocks>UINT32_MAX-TAKE_HEADER_BLOCKS)return fail(w);
 if(reserved_end){
  if(!hint||*hint>reserved_end||reserved_end>d->raw.pool_blocks)return fail(w);
  uint32_t pad=(0u-(d->raw.pool_first+*hint))&15u;
  if(pad>reserved_end-*hint)return fail(w);first=*hint+pad;
  if(blocks+TAKE_HEADER_BLOCKS>reserved_end-first)return fail(w);
 }else if(!cat426_find_room_hint(&w->index,&w->checkpoint,blocks+TAKE_HEADER_BLOCKS,16,d->raw.pool_first,0,0,hint?*hint:0,&first))return fail(w);
 take_metadata m={.format=1,.rate=48000,.frames_per_block=280,.audio_blocks=blocks,.frames=wav.frames,.content_blocks=blocks,.song_frames=wav.frames,.speed_q16=65536,.pan=128,.loop_blocks=blocks};
 if(e->action==IMPORT422_REPLACE){take_metadata old;pool_object audio;if(!cat422_take_objects(w,d,e->audio,e->annotation,&old,&audio))return fail(w);m.pan=old.pan;m.muted=old.muted;m.speed_q16=old.speed_q16;m.start_frames=old.start_frames;}
 pool_writer writer={.generation=w->index.generation,.first=first,.capacity=blocks+TAKE_HEADER_BLOCKS,.crc=UINT32_MAX,.active=true};
 /* Offline-only: canonical PCM16 bypasses converter state, so its already
  * reserved admission scratch can hold16 encoded sectors. Other formats keep
  * the original single-sector path and all decoder workspace. */
 uint8_t*buffer=w->sector;unsigned batch=1;
 if(wav.rate==48000&&wav.format==1&&wav.bits==16&&(wav.codec==0||wav.codec==3)){
  size_t bytes=0;void*borrow=wav_native_work509(&bytes);
  if(borrow&&((uintptr_t)borrow&3u)==0&&bytes>=8192){buffer=borrow;batch=16;}
 }else{void*tail=wav_output_work510(&wav);if(tail){buffer=tail;batch=16;}}
 memset(buffer,0,batch*512);
 for(unsigned at=0;at<TAKE_HEADER_BLOCKS;){unsigned n=TAKE_HEADER_BLOCKS-at;if(n>batch)n=batch;if(!pool_writer_append(d->raw,&writer,buffer,n))return fail(w);at+=n;}
 if(prepared){
  uint32_t prefix_crc=writer.crc;
  for(uint32_t at=0;at<blocks;){unsigned n=blocks-at;if(n>batch)n=batch;
   /*602: the envelope occupies one sector. Start with15 payload sectors,
    * then aligned16-sector reads avoid recurring FAT-cluster tail fragments.
    * Keep the same8KiB buffer and arbitrary final payload length. */
   if(!at&&batch==16&&n==16)n=15;
   if(!e->source.read(e->source.ctx,512+at*512,buffer,n*512))return fail(w);
   if(!native593_blocks(buffer,n)){if(input_error)*input_error=2;return fail(w);}
   if(!pool_writer_append(d->raw,&writer,buffer,n))return fail(w);at+=n;
  }
  /*598: writer already checked every source byte. Remove the captured
   * prefix contribution using the existing CRC linear operator; retain the
   * exact payload comparison before metadata patching and stored readback. */
  uint32_t payload_crc=writer.crc^pool_crc_shift(prefix_crc^UINT32_MAX,(uint64_t)blocks*512);
  if(~payload_crc!=native.crc){if(input_error)*input_error=2;return fail(w);}
 }else{
 wb_encoder encoder={0};int16_t pcm[560];unsigned queued=0;
 int16_t*wide=NULL;
 if(batch==16&&wav.rate==48000&&wav.format==1&&wav.bits==16&&(wav.codec==0||wav.codec==3))wide=overlay513_tail(16u*280u*4u);
 if(wide){
  for(uint32_t i=0;i<blocks;){unsigned take=blocks-i;if(take>16)take=16;uint32_t at=i*280u,n=wav.frames-at;if(n>take*280u)n=take*280u;
   if(!wav_frames(e->source,&wav,at,n,wide,16u*280u))return fail(w);
   memset(wide+n*2u,0,(take*280u-n)*4u);
   for(unsigned j=0;j<take;j++)wb_pack(&encoder,wide+j*560u,buffer+j*512u);
   if(!pool_writer_append(d->raw,&writer,buffer,take))return fail(w);i+=take;
  }
 }else
 for(uint32_t i=0;i<blocks;i++){
  uint32_t at=i*280,n=wav.frames-at;if(n>280)n=280;memset(pcm,0,sizeof pcm);
  if(!wav_frames(e->source,&wav,at,n,pcm,280))return fail(w);wb_pack(&encoder,pcm,buffer+queued*512);queued++;if(queued==batch||i+1==blocks){if(!pool_writer_append(d->raw,&writer,buffer,queued))return fail(w);queued=0;}
 }
 }
 uint32_t audio;if(!cat422_take_stage_object(w,d,&writer,&m,&audio)||!verify(w,d,audio,buffer,batch))return fail(w);
 bool applied=e->action==IMPORT422_ADD?cat422_archive_add(&w->index,e->lane,audio,0):e->archive?cat422_archive_replace(&w->index,e->lane,e->archive,audio,0):cat422_live_replace(&w->index,e->lane,audio,0);
 if(!applied)return fail(w);
 if((current||(e->action==IMPORT422_ADD&&e->lane<64&&!before.audio))&&!music_controls_track(w->controls,e->lane/4,e->lane%4,&m))return fail(w);
 if(hint)*hint=first+writer.written;
 return true;
}
#ifdef IMPORT469
__attribute__((section(".conv469.text.import593wrapper")))
#endif
bool import512_stage(cat422_workspace*w,cat422_device*d,const import422_edit*e,uint32_t*hint,uint32_t end){return import593_stage(w,d,e,hint,end,NULL);}
bool import504_stage(cat422_workspace*w,cat422_device*d,const import422_edit*e,uint32_t*hint){return import512_stage(w,d,e,hint,0);}
bool import422_stage(cat422_workspace*w,cat422_device*d,const import422_edit*e){return import504_stage(w,d,e,0);}
bool import422_controls(cat422_workspace*w,cat422_device*d){
 if(!w||!d||!w->index.ready||!w->index.transaction)return false;bool extended=music_native_snapshot_extended(w->controls);
 for(unsigned song=0;song<16;song++){
  memset(w->loaded,0,sizeof w->loaded);
  for(unsigned t=0;t<4;t++){cat422_lane lane;pool_object audio;if(!cat422_lane_read(&w->index,song*4+t,&lane))return fail(w);if(lane.audio&&!cat422_take_load(w,d,song*4+t,w->loaded+t,&audio))return fail(w);}
  if(!music_native_song_normalize(w->controls,w->loaded,song,extended))return fail(w);
 }
 return cat422_controls_stage(w,d);
}
