#include "stash_ui.h"
#include "backend.h"
#include "paged_bridge.h"
#include "engine.h"
#include "fault.h"
#include "legacy_map.h"
#include "legacy523.h"
#include "sp1_emmc.h"
#include "diskio.h"
#include "storage373.h"
#include <zephyr/sys/printk.h>
#include <zephyr/kernel.h>
#include <string.h>
static music_runtime_retained state;
static uint32_t device_blocks;
static unsigned audit_result;
static write_retry402_trace write402;
extern bool emmc_write_blocks_physical(uint32_t,const uint8_t *,uint32_t);
extern void song373_report(void);
extern void storage415_context_report(uint32_t);
extern void storage418_song_geometry(unsigned,const uint8_t *);
extern void delete435_report(void);
void storage400_report(void){
 delete435_report();
 unsigned f=state.session.writer.fault,reason=(f>>24)&127u,ref=(f>>8)&63u;
 const char *kind=!(f&0x80000000u)?"none":((f>>16)&255u)?"io":reason?"metadata":"transaction";
 printk("FAULT420 class=%s preflight=%u metadata_pending=%u\n",kind,(f&255u)==15,state.session.ownership.writer);

 printk("VALIDATE415 reason=%u song=%u track=%u step=%u io=%u latched=%u\n",reason,reason?ref/4+1:0,reason?ref%4+1:0,f&255u,(f>>16)&255u,(f>>31)&1u);
 if(reason>=64&&reason<=70&&!(f&0x80000000u)){
  const char*why=reason==64?"unavailable":reason==65?"busy":reason==66?"invalid-request":reason==67?"legacy-object-limit":reason==68?"contiguous-audio-reserve":reason==69?"control-reserve":"legacy-take-limit";
  printk("ADMISSION422 refused=%s code=%u fault=0\n",why,reason);
 }
 storage415_context_report(f);
 printk("WRITE412 first_ms=%u recovery_ms=%u\n",write402.first_ms,write402.recovery_ms);
 printk("WRITE406 cleanup=%u stop_probes=%u status_probes=%u r1=%08x\n",write406_trace[0],write406_trace[1],write406_trace[2],write406_trace[3]);
 printk("FAULT400 code=%08x song=%u destination=%u owner=%u writer=%u phase=%u first=%u capacity=%u written=%u generation=%llu active=%u failed=%u\n",state.session.writer.fault,state.session.song,state.session.destination,state.session.ownership.owner,state.session.ownership.writer,state.session.writer_phase,state.session.writer.first,state.session.writer.capacity,state.session.writer.written,(unsigned long long)state.session.writer.generation,state.session.writer.active,state.session.writer.failed);
 printk("WRITE402 first=%08x last=%08x retries=%u recovered=%u exhausted=%u physical=%08x\n",write402.first,write402.last,write402.retries,write402.recovered,write402.exhausted,emmc_write_failure402);
}
bool storage400_fault(void){return music_fault_pending(&state);}
void storage400_failed(void){music_fault_fail(&state,0);state.session.ownership.owner=SP1_STORE_INVALID;}
extern void switch439_report(void);
void storage373_report(void){switch439_report();song373_report();storage400_report();printk("MAP373 song=%u owner=%u writer=%u phase=%u\n",state.session.song,state.session.ownership.owner,state.session.ownership.writer,state.session.writer_phase);printk("STORAGE373 RESULT format=%u blocks=%u\n",audit_result,device_blocks);printk("CAPACITY422 backend=%s\n",music_backend_paged()?"paged":storage523_legacy()?"legacy":"fixed");printk("SETUP539 CAPS radio_direct=1\n");printk("READY427 prepared=%u audition=%u\n",state.session.writer_phase==MUSIC_PREPARED427,state.session.writer_phase==MUSIC_AUDITION427);}
bool storage373_pool(void){return audit_result==MUSIC_MEDIA_POOL;}
bool storage523_legacy(void){return audit_result==MUSIC_MEDIA_LEGACY;}
void storage523_report(void){printk("BASIC523 CAPS legacy=%u takes=%u drive=%u\n",storage523_legacy(),storage373_pool(),storage373_pool());}
#include "cooperative.h"
static bool scheduled;
void storage373_scheduling(bool on){scheduled=on;}
extern bool storage373_service(void),storage373_live(void);
extern int emmc_commit_flush_step(void);
static bool service(void *c){(void)c;bool ok=storage373_service();if(!ok)music_fault_fail(&state,4);return ok;}
static bool raw_rd(void *c,uint32_t b,void *p,uint32_t n){(void)c;return emmc_read_blocks(b,p,n);}
static bool raw_wr(void *c,uint32_t b,const void *p,uint32_t n){(void)c;return emmc_write_blocks_physical(b,p,n);}
static bool retry_wr(void *c,uint32_t b,const void *p,uint32_t n,uint32_t ms){(void)c;return emmc_write_blocks_retry402(b,p,n,ms);}
static uint32_t write_now(void *c){(void)c;return k_uptime_get_32();}
static uint32_t write_reason(void *c){(void)c;return emmc_write_failure402;}
static bool private_raw_wr(void *c,uint32_t b,const void *p,uint32_t n){
 const write_retry402_io io={c,raw_wr,retry_wr,write_now,write_reason};
 return write_retry402(&io,&write402,b,p,n);
}
static int flush_step(void *c){(void)c;int result=storage373_live()?emmc_commit_flush_step():(emmc_cache_flush()?1:-1);if(result<0)music_fault_fail(&state,5);return result;}
/*421: catalog publication writes only the inactive checkpoint. Replay the
 * identical bounded sector span, never restart the transaction or advance its
 * generation twice. The driver must re-establish READY/TRANSFER first.
 * persist() still flushes then verifies all encoded checkpoint bytes. */
static bool commit_raw_wr(void *c,uint32_t b,const void *p,uint32_t n){
 if(n<2)return raw_wr(c,b,p,n); /* single-sector errors remain fail-closed */
 return private_raw_wr(c,b,p,n);
}
static const cooperative_io coop={NULL,service,raw_rd,commit_raw_wr,flush_step};
static const cooperative_io private_coop={NULL,service,raw_rd,private_raw_wr,flush_step};
static bool rd(void *c,uint32_t b,void *p,uint32_t n){bool ok=scheduled?cooperative_read(&coop,b,p,n):raw_rd(c,b,p,n);if(!ok)music_fault_fail(&state,1);return ok;}
static bool wr(void *c,uint32_t b,const void *p,uint32_t n){bool ok=scheduled?cooperative_write(&coop,b,p,n):commit_raw_wr(c,b,p,n);if(!ok)music_fault_fail(&state,2);return ok;}
static bool private_wr(void *c,uint32_t b,const void *p,uint32_t n){bool ok=scheduled?cooperative_write(&private_coop,b,p,n):private_raw_wr(c,b,p,n);if(!ok)music_fault_fail(&state,2);return ok;}
static bool sync_io(void *c){(void)c;bool ok=scheduled?cooperative_flush(&coop):emmc_cache_flush();if(!ok)music_fault_fail(&state,3);return ok;}
DSTATUS disk_initialize(BYTE drive){return drive?STA_NOINIT:0;}
DSTATUS disk_status(BYTE drive){return drive?STA_NOINIT:0;}
DRESULT disk_read(BYTE drive,BYTE *p,LBA_t b,UINT n){if(drive||!n||b>=device_blocks||n>device_blocks-b)return RES_PARERR;return rd(NULL,b,p,n)?RES_OK:RES_ERROR;}
DRESULT disk_ioctl(BYTE drive,BYTE cmd,void *p){if(drive)return RES_PARERR;switch(cmd){case CTRL_SYNC:return RES_OK;case GET_SECTOR_COUNT:*(LBA_t*)p=device_blocks;return RES_OK;case GET_SECTOR_SIZE:*(WORD*)p=512;return RES_OK;case GET_BLOCK_SIZE:*(DWORD*)p=1;return RES_OK;default:return RES_PARERR;}}
void storage373_audit(music_runtime_workspace *w){
 music_session_init(&state.session);uint8_t *p=w->streaming_batch;
 if(!emmc_read_ext_csd(p))return;
 device_blocks=(uint32_t)p[212]|(uint32_t)p[213]<<8|(uint32_t)p[214]<<16|(uint32_t)p[215]<<24;
 pool_io raw={.read=rd,.write=wr,.sync=sync_io,.device_blocks=device_blocks};
 audit_result=music_backend_mount(&state,w,raw,true);
 if(!storage373_pool()){music_session_init(&state.session);state.session.ownership.owner=SP1_STORE_INVALID;}
 printk("STORAGE373 AUDIT format=%u blocks=%u\n",audit_result,device_blocks);
}
bool storage373_read_controls(music_runtime_workspace *w,uint32_t b,uint8_t *p,uint32_t n){
 if(!storage373_pool())return emmc_read_blocks(b,p,n);
 if(!n||b>=6||n>6-b||!music_backend_controls(&state,w,w->stopped_song.controls,true))return false;
 memmove(p,w->stopped_song.controls+b*512,n*512);return true;
}
bool storage373_song(unsigned song,uint32_t first,uint32_t stride,music_runtime_workspace *w){
 if(!storage373_pool())return legacy_map_bind(&state.session,audit_result==MUSIC_MEDIA_LEGACY,song,device_blocks,first,stride);
 bool ok=music_engine_load(&state,w,song,true);
 if(ok)storage418_song_geometry(song,w->stopped_song.controls);
 return ok;
}
bool storage373_map(uint32_t b,uint32_t n,uint32_t first,uint32_t stride,uint32_t *physical){return legacy_map_read(&state.session,b,n,first,stride,physical);}
/* Legacy address writes are never allowed onto FAT media. Pool transactions
 * alone call the physical primitive, after bounded admission/reservation. */
bool emmc_write_blocks(uint32_t b,const uint8_t *p,uint32_t n){return storage523_legacy()&&legacy523_write_range(b,n,device_blocks)&&private_raw_wr(NULL,b,p,n);}
static bool snap(void *ctx,unsigned n,uint8_t out[512]){if(n>=6)return false;memmove(out,(uint8_t*)ctx+n*512,512);return true;}
bool storage373_save(music_runtime_workspace *w){return music_engine_save(&state,w,snap,w->stopped_song.controls,true);}
bool storage401_prepare(music_runtime_workspace *w,unsigned track,uint32_t capacity,bool fresh_replace){
 if(!w||track>=4||storage400_fault())return false;
 if(state.session.ownership.writer)return state.session.destination==state.session.song*4+track;
 if(music_backend_paged()&&music_prepared427_claim(&state.session,track,capacity))return true;
 music_fault_step(&state,MUSIC_PREP_LOAD);
 if(!music_engine_load(&state,w,state.session.song,true))return false;
 /*426: this freshly validated snapshot already belongs to this operation.
  * Do not reload it twice again before reserving the empty recording. */
 if(music_backend_paged())return bridge426_prepare_loaded(&state,w,track,capacity,!fresh_replace&&w->stopped_song.loaded[track].audio_blocks!=0);
 if(!w->stopped_song.loaded[track].audio_blocks)return music_engine_begin(&state,w,track,capacity,false,true);
 return fresh_replace?music_engine_prepare_replace(&state,w,track,capacity,true):music_engine_prepare_edit(&state,w,track,capacity,true);
}
bool storage373_prepare(music_runtime_workspace *w,unsigned track,uint32_t capacity){return storage401_prepare(w,track,capacity,false);}
bool storage373_prepare_step(music_runtime_workspace *w){
 if(storage400_fault())return false;
 if(state.session.writer_phase!=1)return state.session.writer_phase==2;
 if(state.session.writer.written<state.session.tracks[state.session.destination%4].count+TAKE_HEADER_BLOCKS){
  music_fault_step(&state,MUSIC_PREP_COPY);
  return music_session_edit_step(&state.session,state.io,w->streaming_batch,8);
 }
 music_fault_step(&state,MUSIC_PREP_VERIFY);
 if(music_backend_paged())return bridge422_ready(&state,w);
 return music_session_edit_ready(&state.session,&w->stopped_song.pool,state.io);
}
bool storage373_record_ready(unsigned track){return __atomic_load_n(&state.session.writer_phase,__ATOMIC_ACQUIRE)==2&&state.session.destination==state.session.song*4+track;}
bool storage373_writer(void){return state.session.ownership.writer;}
bool storage373_abort(void){return !storage400_fault()&&music_session_abort(&state.session,true);}
uint32_t storage373_written(void){return state.session.writer.written>=TAKE_HEADER_BLOCKS?state.session.writer.written-TAKE_HEADER_BLOCKS:0;}
bool storage373_write(unsigned track,uint32_t offset,const uint8_t *p,uint32_t n,uint8_t scratch[512]){
 pool_io private_io=state.io;private_io.write=private_wr;
 return state.session.destination==state.session.song*4+track&&music_session_write(&state.session,private_io,offset,p,n,scratch);
}
bool storage373_finish(music_runtime_workspace *w,const take_metadata *m){return music_engine_finish_snapshot(&state,w,m,snap,w->stopped_song.controls,true);}
bool storage373_action(music_runtime_workspace *w,unsigned action,unsigned track,unsigned slot){
 if(!storage373_pool()||storage373_writer()||track>=4||slot>=64)return false;
 if(action==22)return music_engine_stash(&state,w,track,slot,true);
 if(action==23)return music_engine_recall(&state,w,slot,track,true);
 if(action==24)return music_engine_drop(&state,w,64+slot,true);
 return false;
}
extern bool emmc_write_blocks_async_physical(uint32_t,const uint8_t *,uint32_t);
bool emmc_write_blocks_async(uint32_t b,const uint8_t *p,uint32_t n){return storage523_legacy()&&legacy523_write_range(b,n,device_blocks)&&emmc_write_blocks_async_physical(b,p,n);}

bool storage381_valid(void){return state.session.ownership.owner!=SP1_STORE_INVALID;}
/* Storage-thread only, existing workspace; no cached catalog or audio buffer. */
int storage384_choose(music_runtime_workspace *w,unsigned track,unsigned action,unsigned requested){
 if(track>=4||!storage373_pool()||storage373_writer()||!music_engine_load(&state,w,state.session.song,true))return -1;
 if(music_backend_paged())return -1; /* retired64-slot protocol */
 pool_store *p=&w->stopped_song.pool;unsigned lane=state.session.song*4+track;int slot=-1;
 if(action==3){for(unsigned i=0;i<64;i++)if(p->current.ref[64+i]==SP1_POOL_NONE)return i;return -1;}
 if(action==4)return pool_lane_selected(&p->current,lane);
 if(action==6){
  if(requested>=64||p->current.ref[64+requested]==SP1_POOL_NONE||p->current.lane[requested])return -1;
  if(!pool_lane_claim(p,lane,requested)||!pool_lane_commit(p))return -1;return requested;
 }
 slot=action==5?(int)requested:pool_lane_browse(&p->current,lane,action==1?-1:1);
 if(slot<0||!pool_lane_select(p,lane,slot)||!pool_lane_commit(p))return -1;
 return slot;
}

#include "takes.h"
int storage397_cycle(music_runtime_workspace *w,unsigned track,int direction,unsigned *ordinal){
 if(!storage373_pool()||storage373_writer())return -1;
 return music_engine_cycle(&state,w,track,direction,ordinal);
}
bool storage397_publish(music_runtime_workspace *w,unsigned track){
 if(track>=4||!storage373_pool()||storage373_writer())return false;
 if(music_backend_paged())return bridge422_publish(&state,w,track);
 pool_store *p=&w->stopped_song.pool;unsigned ref=state.session.song*4+track,id=p->current.ref[ref];
 sp1_extent next={0};
 if(id!=SP1_POOL_NONE){
  if(id>=SP1_POOL_OBJECTS||p->current.object[id].blocks<TAKE_HEADER_BLOCKS)return false;
  next=(sp1_extent){state.io.pool_first+p->current.object[id].first+TAKE_HEADER_BLOCKS,w->stopped_song.loaded[track].audio_blocks};
 }
 /* Target is drained. No reads/writes between mapping publication and the
  * audio-boundary control handoff; peer maps and rings remain untouched. */
 state.session.tracks[track]=next;return true;
}

void storage420_reject(unsigned track,unsigned reason){
 music_fault_step(&state,14);music_fault_context(&state,state.session.song*4+track,reason);music_fault_fail(&state,0);
}

bool storage420_preflight(music_runtime_workspace *w){return music_engine_preflight(&state,w,true);}

void storage427_warm(music_runtime_workspace*w,uint32_t capacity){
 if(music_backend_paged()&&!storage400_fault()&&!state.session.writer_phase)
  (void)bridge427_warm(&state,w,capacity);
}
bool storage427_audition(unsigned track,const take_metadata*m){
 return music_backend_paged()&&!storage400_fault()&&music_audition427(&state.session,state.io,track,m);
}

#include "predict437.h"
int storage437_predict(music_runtime_workspace*w,unsigned track,int direction,switch437_ticket*t){
 if(!music_backend_paged()||!storage373_pool()||storage373_writer())return -2;
 return bridge437_predict(&state,w,track,direction,t);
}
extern void music_engine_refresh437(music_runtime_retained*,music_runtime_workspace*);
bool storage437_finish(music_runtime_workspace*w,const switch437_ticket*t){bool ok=bridge437_finish(&state,w,t);if(ok)music_engine_refresh437(&state,w);return ok;}

bool storage440_extend(music_runtime_workspace*w,switch437_ticket*t,uint32_t actions,unsigned count){return bridge440_extend(&state,w,t,actions,count);}
