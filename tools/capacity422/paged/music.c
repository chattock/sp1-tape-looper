#include "music.h"
#include "../../commit421/runtime/src/control.h"
#include <string.h>
#include <limits.h>
static uint32_t get(const uint8_t*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static bool fail(cat422_workspace*w,pool_writer*p){w->index.ready=false;if(p)p->failed=true;return false;}
static bool read_extent(cat422_device*d,cat422_extent e,uint32_t at,void*b,uint32_t n){return n&&e.first<d->raw.pool_blocks&&e.blocks<=d->raw.pool_blocks-e.first&&at<e.blocks&&n<=e.blocks-at&&d->raw.read(d->raw.context,d->raw.pool_first+e.first+at,b,n);}
bool cat426_take_begin_hint(cat422_workspace*w,cat422_device*d,uint32_t capacity,uint32_t hint,pool_writer*p){
 if(!w||!d||!p||!capacity||capacity>UINT32_MAX-TAKE_HEADER_BLOCKS||!cat426_writer_prepare_hint(&w->index,&w->checkpoint,d,capacity+TAKE_HEADER_BLOCKS,hint,p))return false;
 memset(w->sector,0,512);for(unsigned i=0;i<TAKE_HEADER_BLOCKS;i++)if(!pool_writer_append(d->raw,p,w->sector,1))return fail(w,p);return true;
}
bool cat422_take_begin(cat422_workspace*w,cat422_device*d,uint32_t capacity,pool_writer*p){return cat426_take_begin_hint(w,d,capacity,0,p);}
bool cat422_take_stage_object(cat422_workspace*w,cat422_device*d,pool_writer*p,const take_metadata*m,uint32_t*id){
 if(!w||!d||!p||!w->index.transaction||!p->active||p->failed||p->generation!=w->index.generation||p->written<=TAKE_HEADER_BLOCKS||!take_valid(m)||m->audio_blocks!=p->written-TAKE_HEADER_BLOCKS||!take_encode(m,w->sector))return false;
 uint8_t*previous=w->checkpoint.scratch;
 if(!d->raw.read(d->raw.context,d->raw.pool_first+p->first,previous,1))return fail(w,p);
 for(unsigned i=0;i<512;i++)previous[i]^=w->sector[i];
 uint32_t delta=pool_crc_shift(pool_crc(0,previous,512),(uint64_t)(p->written-1)*512);
 if(!d->raw.write(d->raw.context,d->raw.pool_first+p->first,w->sector,1)||!d->raw.sync(d->raw.context)||!d->raw.read(d->raw.context,d->raw.pool_first+p->first,previous,1)||memcmp(previous,w->sector,512))return fail(w,p);
 p->crc^=delta;return cat422_writer_object(&w->index,d,p,TAKE_TAG,id);
}
bool cat422_take_stage(cat422_workspace*w,cat422_device*d,pool_writer*p,unsigned lane,const take_metadata*m){
 uint32_t id;if(lane>=64||!cat422_take_stage_object(w,d,p,m,&id))return false;return cat422_recorded(&w->index,lane,id,0);
}
bool cat422_take_objects(cat422_workspace*w,cat422_device*d,uint32_t audio_id,uint32_t annotation_id,take_metadata*m,pool_object*audio){
 cat422_extent e;
 if(!w||!d||!m||!audio||!audio_id||!cat422_extent_read(&w->index,audio_id,&e)||e.tag!=TAKE_TAG||e.blocks<=TAKE_HEADER_BLOCKS||!read_extent(d,e,0,w->sector,1)||!take_decode(w->sector,m)||m->audio_blocks!=e.blocks-TAKE_HEADER_BLOCKS)return false;
 if(annotation_id){take_metadata base=*m;cat422_extent a;if(!cat422_extent_read(&w->index,annotation_id,&a)||a.blocks!=1||a.tag!=TAKE_TAG||!read_extent(d,a,0,w->sector,1)||~pool_crc(UINT32_MAX,w->sector,512)!=a.crc||!take_decode(w->sector,m)||m->audio_blocks!=base.audio_blocks||m->format!=base.format||m->frames!=base.frames||m->start_frames!=base.start_frames||m->loop_blocks!=base.loop_blocks||m->content_blocks!=base.content_blocks)return false;}
 *audio=(pool_object){d->raw.pool_first+e.first+TAKE_HEADER_BLOCKS,m->audio_blocks,0,m->format};return true;
}
bool cat422_take_load(cat422_workspace*w,cat422_device*d,unsigned lane,take_metadata*m,pool_object*audio){
 cat422_lane l;return w&&cat422_lane_read(&w->index,lane,&l)&&cat422_take_objects(w,d,l.audio,l.annotation,m,audio);
}
bool cat422_controls_load(cat422_workspace*w,cat422_device*d){
 uint8_t h[32];cat422_extent e;if(!w||!d||!pg422_get(&w->index,0,h)||get(h)!=1||!cat422_extent_read(&w->index,get(h+12),&e)||e.blocks!=6||e.tag!=MUSIC_CONTROL_TAG||!read_extent(d,e,0,w->controls,6))return false;
 return ~pool_crc(UINT32_MAX,w->controls,3072)==e.crc;
}
bool cat422_take_annotate(cat422_workspace*w,cat422_device*d,unsigned lane,const take_metadata*m){
 take_metadata old;pool_object audio;uint32_t first,id;
 if(!w||!d||!m||!w->index.transaction||!take_valid(m)||!cat422_take_load(w,d,lane,&old,&audio))return false;
 if(m->audio_blocks!=old.audio_blocks||m->format!=old.format||m->frames!=old.frames||m->start_frames!=old.start_frames||m->loop_blocks!=old.loop_blocks||m->content_blocks!=old.content_blocks)return false;
 /* Song timing and mixer mute are stored in controls, not new take versions.
  * Match the existing version identity used by the performance UI. */
 if(m->track_head_blocks==old.track_head_blocks&&m->track_tail_blocks==old.track_tail_blocks&&m->pan==old.pan&&m->nudge==old.nudge&&m->content_flags==old.content_flags&&m->magnet_cells==old.magnet_cells&&!memcmp(m->magnet,old.magnet,32))return true;
 /* Comparing canonical bytes avoids struct padding and prevents allocating
  * another metadata object for an unchanged take. */
 if(!take_encode(&old,w->sector)||!take_encode(m,w->checkpoint.scratch))return false;
 if(!memcmp(w->sector,w->checkpoint.scratch,512))return true;
 if(!cat422_find_room(&w->index,&w->checkpoint,1,1,d->raw.pool_first,0,0,&first)||!take_encode(m,w->sector))return false;
 if(!d->raw.write(d->raw.context,d->raw.pool_first+first,w->sector,1)||!d->raw.sync(d->raw.context)||!d->raw.read(d->raw.context,d->raw.pool_first+first,w->checkpoint.scratch,1)||memcmp(w->sector,w->checkpoint.scratch,512))return fail(w,0);
 return cat422_add_extent(&w->index,(cat422_extent){first,1,~pool_crc(UINT32_MAX,w->sector,512),TAKE_TAG},&id)&&cat422_annotated(&w->index,lane,id);
}
bool cat422_controls_stage(cat422_workspace*w,cat422_device*d){
 if(!w||!d||!w->index.transaction)return false;uint32_t first,id;
 if(!cat422_find_room(&w->index,&w->checkpoint,6,1,d->raw.pool_first,0,0,&first))return false;
 if(!d->raw.write(d->raw.context,d->raw.pool_first+first,w->controls,6)||!d->raw.sync(d->raw.context))return fail(w,0);
 for(unsigned i=0;i<6;i++)if(!d->raw.read(d->raw.context,d->raw.pool_first+first+i,w->sector,1)||memcmp(w->sector,w->controls+i*512,512))return fail(w,0);
 return cat422_add_extent(&w->index,(cat422_extent){first,6,~pool_crc(UINT32_MAX,w->controls,3072),MUSIC_CONTROL_TAG},&id)&&cat422_control(&w->index,id);
}
