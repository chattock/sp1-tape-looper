/* Included by engine.c so transaction helpers remain private. */
#include "takes.h"
#define TAKE404_VERSION_FIELDS(X) \
 X(format) X(rate) X(frames_per_block) X(audio_blocks) X(frames) \
 X(content_blocks) X(start_frames) X(track_head_blocks) X(track_tail_blocks) \
 X(pan) X(nudge) X(magnet_cells) X(content_flags) X(loop_blocks)
static bool same_version(const take_metadata *a,const take_metadata *b){
 /* Ignore song-wide timing and mixer mute; retain every take-specific edit. */
#define EQ(f) if(a->f!=b->f)return false;
 TAKE404_VERSION_FIELDS(EQ)
#undef EQ
 return !memcmp(a->magnet,b->magnet,sizeof a->magnet);
}
static bool same_take(pool_store *p,unsigned a,unsigned b,bool *equal){
 *equal=false;if(p->current.ref[a]!=p->current.ref[b])return true;
 if(p->current.annotation[a]==p->current.annotation[b]){*equal=true;return true;}
 take_metadata x,y;pool_object audio;
 if(!take_load(p,a,&x,&audio)||!take_load(p,b,&y,&audio))return false;
 *equal=same_version(&x,&y);return true;
}
int takes_view(pool_store *p,unsigned lane,uint8_t slots[64]){
 if(lane>=64)return -1;unsigned n=0;
 for(unsigned rank=1;rank<=64;rank++)for(unsigned i=0;i<64;i++){
  if((p->current.lane[i]&127)!=lane+1||p->current.rank[i]!=rank)continue;
  bool duplicate=false;
  for(unsigned j=0;j<n;j++){bool equal;if(!same_take(p,64+i,64+slots[j],&equal))return -1;if(equal){duplicate=true;break;}}
  if(!duplicate)slots[n++]=i;
 }return n;
}
/*404: exact version keys use only temporary storage-thread workspace. The
 * first512 bytes remain take_load's sector scratch; no pointer survives load.
 * Compare keys only for the same audio object. take_load validates format,
 * frames, block geometry/start/content/loop against that object's base header;
 * rate and frames_per_block are validated constants. Only the mutable fields
 * below can differ. All64 exact keys fit, without borrowing decoder scratch. */
typedef struct {
 uint32_t head,tail;
 uint16_t magnet_cells;
 uint8_t pan,nudge,content_flags;
 uint8_t magnet[32];
} takes404_key;
enum { TAKES404_SECTOR_KEYS=(SP1_POOL_CATALOG_BYTES-512)/sizeof(takes404_key) };
_Static_assert(sizeof(takes404_key)==48,"bounded version key");
_Static_assert(TAKES404_SECTOR_KEYS>=64,"all keys fit outside take sector scratch");
/* The final512 bytes are disjoint from all64 keys and sector I/O. Reuse this
 * storage-thread scratch instead of stacking metadata above cooperative I/O.
 * metadata is dead after live_key is formed, before any archive read. */
typedef struct {
 take_metadata metadata;
 pool_object audio;
 uint8_t slots[64];
 takes404_key live_key,a,b;
} takes404_scratch;
enum { TAKES404_SCRATCH=512+64*sizeof(takes404_key) };
_Static_assert(TAKES404_SCRATCH+sizeof(takes404_scratch)<=SP1_POOL_CATALOG_BYTES,
 "cache temporaries fit existing catalog scratch");
_Static_assert(__builtin_offsetof(pool_store,bytes)%_Alignof(takes404_scratch)==0&&
 TAKES404_SCRATCH%_Alignof(takes404_scratch)==0,"typed cache scratch alignment");
static takes404_scratch *takes404_temporary(music_runtime_workspace *w){
 return (takes404_scratch *)(void *)(w->stopped_song.pool.bytes+TAKES404_SCRATCH);
}
static void takes404_key_from(takes404_key *out,const take_metadata *in){
 memset(out,0,sizeof(*out));out->head=in->track_head_blocks;out->tail=in->track_tail_blocks;
 out->magnet_cells=(uint16_t)in->magnet_cells;out->pan=(uint8_t)in->pan;
 out->nudge=(uint8_t)in->nudge;out->content_flags=(uint8_t)in->content_flags;
 memcpy(out->magnet,in->magnet,sizeof out->magnet);
}
static uint8_t *takes404_key_at(music_runtime_workspace *w,unsigned slot){
 return w->stopped_song.pool.bytes+512+slot*sizeof(takes404_key);
}
static bool takes404_key_load(music_runtime_workspace *w,unsigned slot,uint64_t *valid,takes404_key *out){
 uint64_t bit=UINT64_C(1)<<slot;
 if(*valid&bit){memcpy(out,takes404_key_at(w,slot),sizeof(*out));return true;}
 takes404_scratch *s=takes404_temporary(w);
 if(!take_load(&w->stopped_song.pool,64+slot,&s->metadata,&s->audio))return false;
 takes404_key_from(out,&s->metadata);
 memcpy(takes404_key_at(w,slot),out,sizeof(*out));*valid|=bit;
 return true;
}
static bool takes404_equal(music_runtime_workspace *w,unsigned a,unsigned b,uint64_t *valid,bool *equal){
 pool_catalog *c=&w->stopped_song.pool.current;*equal=false;
 if(c->ref[64+a]!=c->ref[64+b])return true;
 if(c->annotation[64+a]==c->annotation[64+b]){*equal=true;return true;}
 takes404_scratch *s=takes404_temporary(w);
 if(!takes404_key_load(w,a,valid,&s->a)||!takes404_key_load(w,b,valid,&s->b))return false;
 *equal=!memcmp(&s->a,&s->b,sizeof s->a);return true;
}
static int takes442_number(music_runtime_workspace *w,unsigned lane,uint64_t *valid,unsigned *total){
 pool_catalog *c=&w->stopped_song.pool.current;
 takes404_scratch *s=takes404_temporary(w);unsigned n=0;
 for(unsigned rank=1;rank<=64;rank++)for(unsigned i=0;i<64;i++){
  if((c->lane[i]&127)!=lane+1||c->rank[i]!=rank)continue;
  bool duplicate=false;
  for(unsigned j=0;j<n;j++){bool equal;if(!takes404_equal(w,i,s->slots[j],valid,&equal))return -1;if(equal){duplicate=true;break;}}
  if(!duplicate)s->slots[n++]=i;
 }
 *total=n;
 if(c->ref[lane]==SP1_POOL_NONE)return 0;
 s->metadata=w->stopped_song.loaded[lane%4];
 if(!music_controls_capture(w->stopped_song.controls,lane/4,lane%4,&s->metadata))return -1;
 bool unchanged=same_version(&s->metadata,&w->stopped_song.loaded[lane%4]);
 takes404_key_from(&s->live_key,&s->metadata);
 for(unsigned i=0;i<n;i++){
  unsigned slot=s->slots[i];if(c->ref[64+slot]!=c->ref[lane])continue;
  if(unchanged&&c->annotation[64+slot]==c->annotation[lane])return (int)i+1;
  if(!takes404_key_load(w,slot,valid,&s->a))return -1;
  if(!memcmp(&s->live_key,&s->a,sizeof s->live_key))return (int)i+1;
 }
 /* A just-recorded or edited live version is archived only on the next
  * selection/preservation action. It is the final distinct take meanwhile. */
 *total=n+1;return (int)n+1;
}
static void takes404_refresh(music_runtime_retained *r,music_runtime_workspace *w,unsigned song){
 if(music_backend_paged()){
  cat422_workspace*p=(void*)w;uint32_t values[4],totals[4];
  for(unsigned t=0;t<4;t++){cat422_lane lane;if(!cat422_lane_read(&p->index,song*4+t,&lane)){cache_clear();return;}values[t]=lane.ordinal;totals[t]=lane.count+(lane.audio&&!lane.selected?1u:0u);}
  if(!cat443_preview_publish(&takes_cache.paged,song,values,totals))cache_clear();return;
 }
 pool_store *p=&w->stopped_song.pool;
 uint32_t old=__atomic_load_n(&takes404_cached,__ATOMIC_RELAXED);
 if(song>=16||r->session.song!=song||!p->ready)return;
 if(old!=UINT32_MAX&&(old>>28)==song&&takes404_generation==p->current.generation)return;
 uint64_t valid=0;uint32_t packed=song<<28,preview=song<<28;
 for(unsigned track=0;track<4;track++){
  unsigned total=0;int value=takes442_number(w,song*4+track,&valid,&total);
  if(value<0||value>65||total>65){cache_clear();return;}
  preview|=(uint32_t)total<<(track*7);
  if(p->current.ref[song*4+track]!=SP1_POOL_NONE)packed|=(uint32_t)value<<(track*7);
 }
 takes_cache.legacy.generation_lo=(uint32_t)p->current.generation;
 takes_cache.legacy.generation_hi=(uint32_t)(p->current.generation>>32);
 __atomic_store_n(&takes404_cached,packed,__ATOMIC_RELEASE);
 __atomic_store_n(&takes_cache.legacy.preview,preview,__ATOMIC_RELEASE);
}
uint32_t music_engine_takes404_snapshot(void){return music_backend_paged()||!takes404_generation?UINT32_MAX:__atomic_load_n(&takes404_cached,__ATOMIC_ACQUIRE);}
bool music_engine_take_ordinals(unsigned song,uint32_t values[4]){
 if(music_backend_paged())return cat443_preview_read(&takes_cache.paged,song,values,false);
 uint32_t packed=music_engine_takes404_snapshot();if(song>=16||(packed>>28)!=song)return false;
 for(unsigned i=0;i<4;i++){values[i]=(packed>>(i*7))&127u;if(values[i]>65)return false;}return true;
}
bool music_engine_take_preview442(unsigned song,uint32_t values[4]){
 if(music_backend_paged())return cat443_preview_read(&takes_cache.paged,song,values,true);
 if(!takes404_generation)return false;
 uint32_t packed=__atomic_load_n(&takes_cache.legacy.preview,__ATOMIC_ACQUIRE);
 if(song>=16||(packed>>28)!=song)return false;
 for(unsigned i=0;i<4;i++){values[i]=(packed>>(i*7))&127u;if(values[i]>65)return false;}return true;
}
int music_engine_take404(unsigned song,unsigned track){
 uint32_t ord[4];if(music_backend_paged())return track<4&&music_engine_take_ordinals(song,ord)?(int)ord[track]:-1;
 uint32_t packed=music_engine_takes404_snapshot();
 if(song>=16||track>=4||(packed>>28)!=song)return -1;
 unsigned value=(packed>>(track*7))&127u;return value<=65?(int)value:-1;
}
static int takes_archive_changed(pool_store *p,unsigned lane,const take_metadata *live,bool *changed){
 *changed=false;
 if(lane>=64||p->current.ref[lane]==SP1_POOL_NONE)return -1;
 int free=-1,match=-1;
 for(unsigned i=0;i<64;i++){
  if(p->current.ref[64+i]==SP1_POOL_NONE){if(free<0)free=i;continue;}
  if((p->current.lane[i]&127)!=lane+1||p->current.ref[64+i]!=p->current.ref[lane])continue;
  take_metadata m;pool_object audio;if(!take_load(p,64+i,&m,&audio))return -1;
  if(same_version(&m,live)&&(match<0||p->current.rank[i]<p->current.rank[match]))match=i;
 }
 if(match>=0)return match;
 if(free<0)return -2; /* exact saved-take table refusal, not I/O */
 if(!take_annotate(p,lane,live)||!pool_stage_stash(p,lane,free)||!pool_stage_recall(p,free,lane))return -1;
 *changed=true;return free;
}
int takes_archive(pool_store *p,unsigned lane,const take_metadata *live){bool changed;return takes_archive_changed(p,lane,live,&changed);}
bool takes_delete(pool_store *p,unsigned lane){
 if(lane>=64||p->current.ref[lane]==SP1_POOL_NONE)return false;
 /* Remove aliases of this version only, never another metadata variant,
  * another lane, or the audio of an independently recorded version. */
 for(unsigned i=0;i<64;i++)if((p->current.lane[i]&127)==lane+1){
  bool equal;if(!same_take(p,lane,64+i,&equal))return false;
  if(equal&&!pool_stage_drop(p,64+i))return false;
 }
 return pool_stage_drop(p,lane);
}
static bool takes_preserve(music_runtime_retained *r,music_runtime_workspace *w,unsigned track){
 pool_store *p=&w->stopped_song.pool;unsigned lane=r->session.song*4+track;
 take_metadata *m=&w->stopped_song.loaded[track];
 if(!music_controls_capture(w->stopped_song.controls,lane/4,track,m))return false;
 bool changed;
 if(!pool_writer_room(p,6,1,0,&r->session.writer)){admission_context(r,track);return false;}
 int archived=takes_archive_changed(p,lane,m,&changed);if(archived<0){if(archived==-2)music_fault_context(r,lane,70);return false;}
 /*420: an already preserved identical version needs no duplicate commit. */
 if(!changed)return true;
 /* Publish the protected old version before any writer reservation/copy.
  * Cancellation creates no new audio version; an existing alias is reused. */
 if(!music_control_save(p,snapshot,w->stopped_song.controls)){invalid(r);return false;}
 return music_engine_load(r,w,lane/4,true);
}
int music_engine_cycle(music_runtime_retained *r,music_runtime_workspace *w,unsigned track,int direction,unsigned *ordinal){
 if(music_backend_paged()){if(!quiet(r,true))return -1;int result=bridge422_cycle(r,w,track,direction,ordinal);if(result>=0)takes404_refresh(r,w,r->session.song);return result;}
 if(!quiet(r,true)||track>=4||(direction!=-1&&direction!=1)||!music_engine_load(r,w,r->session.song,true))return -1;
 unsigned lane=r->session.song*4+track;pool_store *p=&w->stopped_song.pool;uint8_t *c=w->stopped_song.controls;
 music_fault_step(r,MUSIC_PREP_LOAD);
 if(!pool_writer_room(p,6,1,0,&r->session.writer)){admission_context(r,track);return -1;}
 int current=-1;bool live=p->current.ref[lane]!=SP1_POOL_NONE;
 if(live){
  take_metadata m=w->stopped_song.loaded[track];
  if(!music_controls_capture(c,lane/4,track,&m))return -1;
  current=takes_archive(p,lane,&m);if(current<0){if(current==-2)music_fault_context(r,lane,70);return -1;}
 }
 uint8_t slots[64];int n=takes_view(p,lane,slots);if(n<0)return -1;
 int pos=n;for(int i=0;i<n;i++)if(slots[i]==current)pos=i;
 /* DOWN cycles recorded versions only. UP always selects the single
  * empty position; it never allocates another lane or erases a take. */
 int next=direction>0||!n?n:(pos+n-1)%n;
 int slot=next==n?-1:slots[next];
 if(!live && slot<0){*ordinal=0;return 64;} /* already New: no catalog write */
 take_metadata target;pool_object audio;
 if(slot>=0&&!take_load(p,64+slot,&target,&audio))return -1;
 /* Keep the lane's live mute when changing content. */
 if(slot>=0)target.muted=(c[1008+lane/4]>>(4+track))&1u;
 if(!music_controls_track(c,lane/4,track,slot<0?NULL:&target))return -1;
 p->current.ref[lane]=slot<0?SP1_POOL_NONE:p->current.ref[64+slot];
 p->current.annotation[lane]=slot<0?0:p->current.annotation[64+slot];
 for(unsigned i=0;i<64;i++)if((p->current.lane[i]&127)==lane+1)p->current.lane[i]&=127;
 if(slot>=0)p->current.lane[slot]|=128;
 memset(&w->stopped_song.loaded[track],0,sizeof(target));
 if(slot>=0)w->stopped_song.loaded[track]=target;
 if(!music_native_song_validate(c,w->stopped_song.loaded,lane/4))return -1;
 if(!music_control_save(p,snapshot,c)){invalid(r);return -1;}
 *ordinal=slot<0?0:(unsigned)next+1;
 takes404_refresh(r,w,lane/4);
 /* Deliberately leave r->session.tracks on the old committed mapping. Caller
  * fades/drains the target at an audio boundary, then loads/publishes once. */
 return slot<0?64:slot;
}

/*429: queried only by the storage worker after successful selection. */
unsigned music_engine_feedback429(music_runtime_workspace*w,unsigned song,unsigned track,unsigned ordinal){
 if(ordinal)return ordinal;
 if(song>=16||track>=4)return 0;
 if(music_backend_paged()){cat422_workspace*p=(void*)w;cat422_lane lane;
  return cat422_lane_read(&p->index,song*4+track,&lane)?lane.count+1u:0u;}
 uint8_t slots[64];int n=takes_view(&w->stopped_song.pool,song*4+track,slots);
 return n>=0?(unsigned)n+1u:0u;
}

unsigned music_engine_empty_saved430(unsigned song){return music_backend_paged()?cat443_preview_empty(&takes_cache.paged,song):0u;}

/* Companion only: bounded RAM cache read, no catalog traversal or retries. */
bool music_engine_take_lanes517(unsigned song,uint32_t ord[4],uint32_t totals[4]){
 /* Legacy cache has no atomic paired publication; never mix its two fields. */
 return music_backend_paged()&&cat517_preview_lanes(&takes_cache.paged,song,ord,totals);
}
