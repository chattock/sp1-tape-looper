#include "engine.h"
#include "controls.h"
#include "snapshot.h"
#include "fault.h"
#include "paged_bridge.h"
#include <string.h>
static bool takes_room(const pool_store *p){for(unsigned i=64;i<128;i++)if(p->current.ref[i]==SP1_POOL_NONE)return true;return false;}
static bool takes_admit(music_runtime_retained*r,const pool_store*p,unsigned track){if(takes_room(p))return true;music_fault_context(r,r->session.song*4+track,70);return false;}
static void admission_context(music_runtime_retained*r,unsigned track){unsigned reason=(r->session.writer.fault>>24)&127u;if(reason>=64&&reason<=70)music_fault_context(r,r->session.song*4+track,reason);}
static bool quiet(music_runtime_retained *r,bool stopped){return r&&stopped&&!r->session.ownership.writer&&!r->session.ownership.readers;}
static bool snapshot(void *ctx,unsigned sector,uint8_t out[512]){if(sector>=6)return false;memcpy(out,(uint8_t*)ctx+sector*512,512);return true;}
/*443: word-aligned legacy generation avoids padding the20-byte cache to24.
 * Storage owns generation; zero means the legacy cache has not been loaded.
 * Zero initialization avoids extra data-section padding; total RAM unchanged. */
static union {struct {uint32_t cached,preview,generation_lo,generation_hi;} legacy;cat443_preview paged;} takes_cache;
_Static_assert(sizeof(takes_cache)==20,"bounded total/ordinal UI cache");
#define takes404_cached takes_cache.legacy.cached
#define takes404_generation ((uint64_t)takes_cache.legacy.generation_hi<<32|takes_cache.legacy.generation_lo)
static void cache_clear(void){if(music_backend_paged())cat443_preview_clear(&takes_cache.paged);else {__atomic_store_n(&takes404_cached,UINT32_MAX,__ATOMIC_RELEASE);__atomic_store_n(&takes_cache.legacy.preview,UINT32_MAX,__ATOMIC_RELEASE);}}

static void takes404_refresh(music_runtime_retained *,music_runtime_workspace *,unsigned);
static void invalid(music_runtime_retained *r){cache_clear();music_fault_fail(r,0);r->session.ownership.owner=SP1_STORE_INVALID;memset(r->session.tracks,0,sizeof(r->session.tracks));}
bool music_engine_load(music_runtime_retained *r,music_runtime_workspace *w,unsigned song,bool stopped){
 if(!quiet(r,stopped)||!w||song>=16)return false;
 if(r->session.ownership.owner!=SP1_STORE_PERFORMANCE)cache_clear();
 bool ok=music_backend_load(r,w,song,true)&&
 music_native_song_normalize(w->stopped_song.controls,w->stopped_song.loaded,song,music_native_snapshot_extended(w->stopped_song.controls));
 if(!ok)invalid(r);else takes404_refresh(r,w,song);
 return ok;
}
#include "takes.inc"
bool music_engine_stash(music_runtime_retained *r,music_runtime_workspace *w,unsigned track,unsigned stash,bool stopped){
 if(!quiet(r,stopped)||!w||track>=4||stash>=64)return false;
 if(music_backend_paged())return false; /* superseded slot protocol; no truncation */
 unsigned song=r->session.song;
 if(!music_engine_load(r,w,song,true))return false;
 pool_store *p=&w->stopped_song.pool;uint8_t *c=w->stopped_song.controls;
 if(!w->stopped_song.loaded[track].audio_blocks||p->current.ref[64+stash]!=SP1_POOL_NONE)return false;
 if(!music_controls_capture(c,song,track,&w->stopped_song.loaded[track]))return false;
 if(!music_controls_track(c,song,track,NULL))return false;
 if(!take_annotate(p,song*4+track,&w->stopped_song.loaded[track])||
    !pool_stage_stash(p,song*4+track,stash)||!music_control_save(p,snapshot,c)){invalid(r);return false;}
 return music_engine_load(r,w,song,true);
}
bool music_engine_recall(music_runtime_retained *r,music_runtime_workspace *w,unsigned stash,unsigned track,bool stopped){
 if(!quiet(r,stopped)||!w||track>=4||stash>=64)return false;
 if(music_backend_paged())return false;
 unsigned song=r->session.song;
 if(!music_engine_load(r,w,song,true))return false;
 pool_store *p=&w->stopped_song.pool;uint8_t *c=w->stopped_song.controls;
 if(w->stopped_song.loaded[track].audio_blocks || (p->current.lane[stash]&127)!=song*4+track+1)return false;
 take_metadata *m=&w->stopped_song.loaded[track];pool_object audio;
 if(!take_load(p,64+stash,m,&audio)||!music_controls_track(c,song,track,m))return false;
 if(!music_session_recall_control(&r->session,p,r->io,stash,track,true,snapshot,c,m))return false;
 return music_engine_load(r,w,song,true);
}
bool music_engine_begin(music_runtime_retained *r,music_runtime_workspace *w,unsigned track,uint32_t capacity,bool edit,bool stopped){
 if(!quiet(r,stopped)||!w||track>=4)return false;
 if(music_backend_paged()){if(!bridge422_load(r,w,r->session.song)||edit!=(w->stopped_song.loaded[track].audio_blocks!=0))return false;return bridge426_prepare_loaded(r,w,track,capacity,edit);}
 if(!music_engine_load(r,w,r->session.song,true))return false;
 bool present=w->stopped_song.loaded[track].audio_blocks!=0;
 if(edit!=present)return false; /* no silent replacement of an occupied take */
 if(edit&&!takes_preserve(r,w,track))return false;
 if(!takes_admit(r,&w->stopped_song.pool,track))return false;
 bool ok=edit?music_session_edit_capacity(&r->session,&w->stopped_song.pool,r->io,track,capacity,true):
 music_session_record(&r->session,&w->stopped_song.pool,r->io,track,capacity,true);
 if(!ok)admission_context(r,track);
 if(!ok&&(!w->stopped_song.pool.ready||r->session.writer.failed))invalid(r);
 return ok;
}
bool music_engine_finish(music_runtime_retained *r,music_runtime_workspace *w,const take_metadata *m,bool stopped){
 if(!r||!w||!m||!stopped||!r->session.ownership.writer||r->session.ownership.readers||r->session.destination>=64)return false;
 if(music_backend_paged()){cat422_workspace*p=(void*)w;if(!bridge422_open(r,w)||!cat422_controls_load(p,&p->device))return false;return bridge422_finish(r,w,m,snapshot,p->controls);}
 /* Recording reuses all workspace. Reload fresh committed controls rather than
  * retaining any buffer from begin. The writer remains in retained session. */
 pool_store *p=&w->stopped_song.pool;uint8_t *c=w->stopped_song.controls;
 if(!pool_open(p,r->io)||!music_control_verify(p)){invalid(r);return false;}
 for(unsigned i=0;i<6;i++)if(!music_control_read(p,i,c+i*512)){invalid(r);return false;}
 unsigned song=r->session.destination/4,track=r->session.destination%4;
 if(!music_controls_track(c,song,track,m))return false;
 if(!music_session_finish_control(&r->session,p,r->io,m,true,snapshot,c))return false;
 return music_engine_load(r,w,song,true);
}

bool music_engine_drop(music_runtime_retained *r,music_runtime_workspace *w,unsigned ref,bool stopped){
 if(!quiet(r,stopped)||!w||ref>=128||(ref<64&&ref/4!=r->session.song))return false;
 if(music_backend_paged())return bridge422_drop(r,w,ref);
 unsigned song=r->session.song;
 if(!music_engine_load(r,w,song,true))return false;
 pool_store *p=&w->stopped_song.pool;uint8_t *c=w->stopped_song.controls;
 if(p->current.ref[ref]==SP1_POOL_NONE||!pool_record_room(p,6,1,0))return false;
 if(ref<64&&!music_controls_track(c,song,ref%4,NULL))return false;
 if(!(ref<64?takes_delete(p,ref):pool_stage_drop(p,ref))||!music_control_save(p,snapshot,c)){invalid(r);return false;}
 return music_engine_load(r,w,song,true);
}

bool music_engine_prepare_edit(music_runtime_retained *r,music_runtime_workspace *w,unsigned track,uint32_t capacity,bool stopped){
 if(!quiet(r,stopped)||!w||track>=4)return false;
 if(music_backend_paged())return bridge422_prepare(r,w,track,capacity,true);
 music_fault_step(r,MUSIC_PREP_LOAD);
 if(!music_engine_load(r,w,r->session.song,true))return false;
 music_fault_step(r,MUSIC_PREP_PRESERVE);
 if(!takes_preserve(r,w,track)||!takes_admit(r,&w->stopped_song.pool,track))return false;
 music_fault_step(r,MUSIC_PREP_RESERVE);
 bool ok=music_session_edit_prepare(&r->session,&w->stopped_song.pool,r->io,track,capacity,true);
 if(!ok)admission_context(r,track);
 /* Reservation initializes the writer. Restore only its missing context;
  * never replace an I/O category or an already recorded failure stage. */
 if(!(r->session.writer.fault&255u))r->session.writer.fault|=MUSIC_PREP_RESERVE;
 return ok;
}
bool music_engine_prepare_replace(music_runtime_retained *r,music_runtime_workspace *w,unsigned track,uint32_t capacity,bool stopped){
 if(!quiet(r,stopped)||!w||track>=4)return false;
 if(music_backend_paged())return bridge422_prepare(r,w,track,capacity,false);
 music_fault_step(r,MUSIC_PREP_LOAD);
 if(!music_engine_load(r,w,r->session.song,true))return false;
 if(!w->stopped_song.loaded[track].audio_blocks)return false;
 music_fault_step(r,MUSIC_PREP_PRESERVE);
 if(!takes_preserve(r,w,track)||!takes_admit(r,&w->stopped_song.pool,track))return false;
 music_fault_step(r,MUSIC_PREP_RESERVE);
 bool ok=music_session_record(&r->session,&w->stopped_song.pool,r->io,track,capacity,true);
 if(!(r->session.writer.fault&255u))r->session.writer.fault|=MUSIC_PREP_RESERVE;
 if(!ok)admission_context(r,track);
 if(!ok&&(!w->stopped_song.pool.ready||r->session.writer.failed))invalid(r);
 return ok;
}
static bool native_snapshot(music_runtime_retained *r,pool_store *p,take_metadata tracks[4],uint8_t *c){
 bool extended=music_native_snapshot_extended(c);
 for(unsigned song=0;song<16;song++){
  memset(tracks,0,4*sizeof(*tracks));
  for(unsigned t=0;t<4;t++)if(p->current.ref[song*4+t]!=SP1_POOL_NONE){
   pool_object a;if(!take_load(p,song*4+t,tracks+t,&a)){music_fault_context(r,song*4+t,1);return false;}
  }
  unsigned issue=music_native_song_issue(c,tracks,song,extended);
  if(issue){music_fault_context(r,song*4+(issue&3u),issue>>2);return false;}
  if(!music_native_song_normalize(c,tracks,song,extended))return false;
 }
 return true;
}
bool music_engine_preflight(music_runtime_retained *r,music_runtime_workspace *w,bool stopped){
 if(!quiet(r,stopped)||!w)return false;
 if(music_backend_paged())return bridge422_preflight(r,w);
 music_fault_step(r,15);
 pool_store *p=&w->stopped_song.pool;
 if(!pool_open(p,r->io)||!music_control_load(p,w->stopped_song.controls)||
    !native_snapshot(r,p,w->stopped_song.loaded,w->stopped_song.controls)){invalid(r);return false;}
 /* Read-only boot admission. Restore selected maps after all-song scratch. */
 return music_engine_load(r,w,r->session.song,true);
}
bool music_engine_save(music_runtime_retained *r,music_runtime_workspace *w,music_control_snapshot capture,void *ctx,bool stopped){
 if(!quiet(r,stopped)||!w||!capture)return false;
 if(music_backend_paged()){bool ok=bridge422_save(r,w,capture,ctx);if(ok)takes404_refresh(r,w,r->session.song);return ok;}
 pool_store *p=&w->stopped_song.pool;uint8_t *c=w->stopped_song.controls;
 if(!pool_open(p,r->io)||!music_control_verify(p)){invalid(r);return false;}
 for(unsigned i=0;i<6;i++)if(!capture(ctx,i,c+i*512))return false;
 /* Missing entries delete only their own reference; never synthesize audio. */
 for(unsigned s=0;s<16;s++)for(unsigned t=0;t<4;t++){
  unsigned present=c[8+s*44+8+t];if(present>1)return false;
  if(!present&&p->current.ref[s*4+t]!=SP1_POOL_NONE&&!takes_delete(p,s*4+t)){invalid(r);return false;}
 }
 if(!native_snapshot(r,p,w->stopped_song.loaded,c))return false;
 if(!music_control_save(p,snapshot,c)){invalid(r);return false;}
 return music_engine_load(r,w,r->session.song,true);
}
bool music_engine_finish_snapshot(music_runtime_retained *r,music_runtime_workspace *w,const take_metadata *m,music_control_snapshot capture,void *ctx,bool stopped){
 if(!r||!w||!m||!capture||!stopped||(r->session.writer_phase!=2&&r->session.writer_phase!=MUSIC_AUDITION427)||!r->session.ownership.writer||r->session.ownership.readers||r->session.destination>=64)return false;
 if(music_backend_paged()){bool ok=bridge422_finish(r,w,m,capture,ctx);if(ok)takes404_refresh(r,w,r->session.song);return ok;}
 music_fault_step(r,1);
 pool_store *p=&w->stopped_song.pool;uint8_t *c=w->stopped_song.controls;
 if(!pool_open(p,r->io)||!music_control_verify(p)){invalid(r);return false;}
 music_fault_step(r,2);
 for(unsigned i=0;i<6;i++)if(!capture(ctx,i,c+i*512)){invalid(r);return false;}
 unsigned song=r->session.destination/4,track=r->session.destination%4;
 music_fault_step(r,3);
 w->stopped_song.loaded[track]=*m; /*419: retain failed candidate, no extra RAM*/
 if(!music_controls_track(c,song,track,m)){music_fault_context(r,song*4+track,14);invalid(r);return false;}
 music_fault_step(r,4);
 if(!take_stage(p,&r->session.writer,r->session.destination,m,true)){invalid(r);return false;}
 music_fault_step(r,5);
 for(unsigned s=0;s<16;s++)for(unsigned t=0;t<4;t++){
  unsigned present=c[8+s*44+8+t];
  if(present>1||(!present&&p->current.ref[s*4+t]!=SP1_POOL_NONE&&!takes_delete(p,s*4+t))){invalid(r);return false;}
 }
 music_fault_step(r,6);
 if(!native_snapshot(r,p,w->stopped_song.loaded,c)){invalid(r);return false;}
 music_fault_step(r,7);
 if(!music_control_save(p,snapshot,c)){invalid(r);return false;}
 __atomic_store_n(&r->session.writer_phase,0,__ATOMIC_RELEASE);sp1_writer_release(&r->session.ownership);
 music_fault_step(r,8);
 return music_engine_load(r,w,song,true);
}

void music_engine_refresh437(music_runtime_retained*r,music_runtime_workspace*w){takes404_refresh(r,w,r->session.song);}
