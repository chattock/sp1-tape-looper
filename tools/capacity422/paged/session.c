#include "session.h"
#include "../../commit421/runtime/src/controls.h"
#include "../../commit421/runtime/src/snapshot.h"
#include <string.h>
#include <limits.h>
static bool quiet(const music_session*s){return s&&!s->ownership.readers&&!s->ownership.writer&&(s->ownership.owner==SP1_STORE_VALIDATING||s->ownership.owner==SP1_STORE_PERFORMANCE);}
static bool invalid(music_session*s){if(s){memset(s->tracks,0,sizeof s->tracks);s->ownership.owner=SP1_STORE_INVALID;s->writer.failed=true;}return false;}
bool cat427_session_warm(music_session*s,cat422_workspace*w,cat422_device*d,uint32_t capacity){
 if(!quiet(s)||!w||!d||s->ownership.owner!=SP1_STORE_PERFORMANCE||s->writer_phase||!capacity)return false;
 bool empty=false;for(unsigned i=0;i<4;i++)empty|=s->tracks[i].count==0;
 if(!empty){s->writer_phase=MUSIC_NO_RESERVE427;return false;}
 uint32_t hint=s->writer.first<d->raw.pool_blocks&&s->writer.written<=d->raw.pool_blocks-s->writer.first?s->writer.first+s->writer.written:0;
 bool ok=cat426_take_begin_hint(w,d,capacity,hint,&s->writer);
 __atomic_store_n(&s->writer_phase,ok?MUSIC_PREPARED427:MUSIC_NO_RESERVE427,__ATOMIC_RELEASE);
 return ok;
}
static bool geometry(cat422_workspace*w,cat422_device*d){
 bool extended=music_native_snapshot_extended(w->controls);
 for(unsigned song=0;song<16;song++){
  memset(w->loaded,0,sizeof w->loaded);
  for(unsigned t=0;t<4;t++){cat422_lane lane;pool_object audio;if(!cat422_lane_read(&w->index,song*4+t,&lane))return false;if(lane.audio&&!cat422_take_objects(w,d,lane.audio,lane.annotation,w->loaded+t,&audio))return false;}
  if(!music_native_song_normalize(w->controls,w->loaded,song,extended))return false;
 }
 return true;
}
static bool removals(cat422_workspace*w){
 for(unsigned song=0;song<16;song++)for(unsigned t=0;t<4;t++){
  unsigned present=w->controls[8+song*44+8+t];cat422_lane lane;
  if(present>1||!cat422_lane_read(&w->index,song*4+t,&lane))return false;
  if(!present&&lane.audio&&!cat422_delete(&w->index,song*4+t))return false;
 }
 return true;
}
bool cat439_session_load_open(music_session*s,cat422_workspace*w,cat422_device*d,unsigned song){
 music_prepared427_revoke(s);
 if(!quiet(s)||!w||!d||song>=16)return false;
 if(!w->index.ready||w->index.transaction||w->index.io.ctx!=d||!cat422_controls_load(w,d))return invalid(s);
 sp1_extent pending[4]={{0}};memset(w->loaded,0,sizeof w->loaded);
 for(unsigned t=0;t<4;t++){cat422_lane lane;if(!cat422_lane_read(&w->index,song*4+t,&lane))return invalid(s);if(lane.audio){pool_object map;if(!cat422_take_load(w,d,song*4+t,w->loaded+t,&map))return invalid(s);pending[t]=(sp1_extent){map.first,map.blocks};}}
 if(!music_native_song_normalize(w->controls,w->loaded,song,music_native_snapshot_extended(w->controls)))return invalid(s);
 memcpy(s->tracks,pending,sizeof pending);s->song=song;s->ownership.owner=SP1_STORE_PERFORMANCE;return true;
}
bool cat422_session_load(music_session*s,cat422_workspace*w,cat422_device*d,unsigned song){
 music_prepared427_revoke(s);
 if(!quiet(s)||!w||!d||song>=16)return false;
 if(!cat422_device_open(&w->index,d))return invalid(s);
 return cat439_session_load_open(s,w,d,song);
}
bool cat422_session_prepare(music_session*s,cat422_workspace*w,cat422_device*d,unsigned track,uint32_t capacity,bool clone){
 music_prepared427_revoke(s);
 if(!quiet(s)||s->ownership.owner!=SP1_STORE_PERFORMANCE||!w||!d||track>=4||!capacity||capacity>UINT32_MAX-TAKE_HEADER_BLOCKS||!cat422_device_open(&w->index,d))return false;
 cat422_lane lane;unsigned destination=s->song*4+track;if(s->song>=16||!cat422_lane_read(&w->index,destination,&lane))return false;
 if(lane.audio){
  pool_object audio;
  if(!cat422_take_load(w,d,destination,w->loaded+track,&audio)||!music_controls_capture(w->controls,s->song,track,w->loaded+track)||!cat422_begin(&w->index)||!cat422_take_annotate(w,d,destination,w->loaded+track)||!cat422_preserve(&w->index,destination)||!cat422_controls_stage(w,d)||!cat422_commit(&w->index))return invalid(s);
 }
 /*426: start searching after the preceding writer rather than walking past
  * the entire same history for every new take. The hint can be stale/aborted:
  * allocator checks all live roots and wraps, so it grants no write rights. */
 uint32_t hint=s->writer.first<d->raw.pool_blocks&&s->writer.written<=d->raw.pool_blocks-s->writer.first?s->writer.first+s->writer.written:0;
 if(clone){take_metadata meta;pool_object audio;if(!cat422_take_load(w,d,s->song*4+track,&meta,&audio)||audio.blocks>capacity||audio.first!=s->tracks[track].first||audio.blocks!=s->tracks[track].count||!cat426_writer_prepare_hint(&w->index,&w->checkpoint,d,capacity+TAKE_HEADER_BLOCKS,hint,&s->writer))return false;}
 else if(!cat426_take_begin_hint(w,d,capacity,hint,&s->writer))return false;
 s->destination=s->song*4+track;__atomic_store_n(&s->writer_phase,clone?1:2,__ATOMIC_RELEASE);return sp1_writer_acquire(&s->ownership);
}
bool cat422_session_edit_ready(music_session*s,cat422_workspace*w,cat422_device*d){
 if(!s||!w||!d||s->writer_phase!=1||!s->ownership.writer||s->ownership.readers||s->writer.failed||s->destination>=64)return false;
 cat422_lane lane;cat422_extent e;sp1_extent map=s->tracks[s->destination%4];
 if(!cat422_device_open(&w->index,d)||w->index.generation!=s->writer.generation||!cat422_lane_read(&w->index,s->destination,&lane)||!cat422_extent_read(&w->index,lane.audio,&e)||e.blocks!=s->writer.written||d->raw.pool_first+e.first+TAKE_HEADER_BLOCKS!=map.first||e.crc!=~s->writer.crc)return invalid(s);
 __atomic_store_n(&s->writer_phase,2,__ATOMIC_RELEASE);return true;
}
bool cat422_session_finish(music_session*s,cat422_workspace*w,cat422_device*d,const take_metadata*m){
 if(!s||!w||!d||!m||(s->writer_phase!=2&&s->writer_phase!=MUSIC_AUDITION427)||!s->ownership.writer||s->ownership.readers||s->destination>=64)return false;
 if(!cat422_device_open(&w->index,d)||w->index.generation!=s->writer.generation||!cat422_begin(&w->index))return invalid(s);
 unsigned song=s->destination/4,track=s->destination%4;
 /* controls must be the caller's stable current snapshot, not stale state
  * retained across capture. All-song validation precedes publication. */
 if(!music_controls_track(w->controls,song,track,m)||!cat422_take_stage(w,d,&s->writer,s->destination,m))return invalid(s);
 if(!removals(w)||!geometry(w,d))return invalid(s);
 if(!cat422_controls_stage(w,d)||!cat422_commit(&w->index))return invalid(s);
 __atomic_store_n(&s->writer_phase,0,__ATOMIC_RELEASE);if(!sp1_writer_release(&s->ownership))return invalid(s);
 return cat422_session_load(s,w,d,song);
}
bool cat422_session_preflight(music_session*s,cat422_workspace*w,cat422_device*d){
 music_prepared427_revoke(s);
 if(!quiet(s)||!w||!d)return false;
 if(!cat422_device_open(&w->index,d)||!cat422_controls_load(w,d)||!geometry(w,d))return invalid(s);
 return cat422_session_load(s,w,d,s->song);
}
bool cat422_session_save(music_session*s,cat422_workspace*w,cat422_device*d){
 music_prepared427_revoke(s);
 if(!quiet(s)||!w||!d||s->ownership.owner!=SP1_STORE_PERFORMANCE)return false;
 if(!cat422_device_open(&w->index,d)||!cat422_begin(&w->index)||!removals(w)||!geometry(w,d)||!cat422_controls_stage(w,d)||!cat422_commit(&w->index))return invalid(s);
 return cat422_session_load(s,w,d,s->song);
}
bool cat422_session_select(music_session*s,cat422_workspace*w,cat422_device*d,unsigned track,int direction){
 music_prepared427_revoke(s);
 if(!quiet(s)||!w||!d||track>=4||s->song>=16||s->ownership.owner!=SP1_STORE_PERFORMANCE)return false;
 unsigned song=s->song,lane=song*4+track;cat422_lane current;pool_object audio;
 if(!cat422_device_open(&w->index,d)||!cat422_lane_read(&w->index,lane,&current))return invalid(s);
 if((!direction&&!current.audio)||(direction<0&&!current.audio&&!current.count))return false;
 if(!cat422_begin(&w->index))return invalid(s);
 if(direction&&current.audio){
  if(!cat422_take_load(w,d,lane,w->loaded+track,&audio)||!music_controls_capture(w->controls,song,track,w->loaded+track)||!cat422_take_annotate(w,d,lane,w->loaded+track))return invalid(s);
 }
 if(!(direction>0?cat422_new_take(&w->index,lane):direction<0?cat422_cycle(&w->index,lane):cat422_delete(&w->index,lane)))return invalid(s);
 if(!cat422_lane_read(&w->index,lane,&current))return invalid(s);
 if(current.audio){if(!cat422_take_load(w,d,lane,w->loaded+track,&audio)||!music_controls_track(w->controls,song,track,w->loaded+track))return invalid(s);}
 else if(!music_controls_track(w->controls,song,track,0))return invalid(s);
 if(!cat422_controls_stage(w,d)||!cat422_commit(&w->index))return invalid(s);
 /* Publication of maps remains behind the caller's drain/fade barrier. */
 /*436: the selected metadata is already validated and controls read back.
  * Publish only the target extent; leave peer mappings and metadata intact.
  * The caller still owns the fade/drain barrier before audible publication. */
 if(current.audio)s->tracks[track]=(sp1_extent){audio.first,audio.blocks};
 else {s->tracks[track]=(sp1_extent){0};memset(w->loaded+track,0,sizeof w->loaded[track]);}
 return music_native_song_normalize(w->controls,w->loaded,song,music_native_snapshot_extended(w->controls));
}
/* A clean archived selection was resolved from the current open generation.
 * Publish just the final target, with the same controls/readback/root barriers
 * as ordinary selection. Intermediate browse positions never need a save. */
bool cat440_session_select_open(music_session*s,cat422_workspace*w,cat422_device*d,unsigned track,uint32_t selected,uint32_t ordinal,uint64_t generation){
 music_prepared427_revoke(s);
 if(!quiet(s)||!w||!d||track>=4||s->song>=16||s->ownership.owner!=SP1_STORE_PERFORMANCE||!w->index.ready||w->index.transaction||w->index.io.ctx!=d||w->index.generation!=generation)return false;
 unsigned song=s->song,lane=song*4+track;cat422_lane current;pool_object audio;
 if(!cat422_begin(&w->index)||!cat440_select(&w->index,lane,selected,ordinal)||!cat422_lane_read(&w->index,lane,&current))return invalid(s);
 if(current.audio){if(!cat422_take_load(w,d,lane,w->loaded+track,&audio)||!music_controls_track(w->controls,song,track,w->loaded+track))return invalid(s);}
 else if(!music_controls_track(w->controls,song,track,0))return invalid(s);
 if(!cat422_controls_stage(w,d)||!cat422_commit(&w->index))return invalid(s);
 if(current.audio)s->tracks[track]=(sp1_extent){audio.first,audio.blocks};
 else {s->tracks[track]=(sp1_extent){0};memset(w->loaded+track,0,sizeof w->loaded[track]);}
 return music_native_song_normalize(w->controls,w->loaded,song,music_native_snapshot_extended(w->controls));
}
