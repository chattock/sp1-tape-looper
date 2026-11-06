#include "backend.h"
#include <string.h>
#include "paged_bridge.h"
#include "legacy523.h"
static bool paged_media;
bool music_backend_paged(void){return paged_media;}
static void reject(music_runtime_retained *r){
 music_session_init(&r->session);r->session.ownership.owner=SP1_STORE_INVALID;
 memset(&r->io,0,sizeof(r->io));
}
static bool borrowable(const music_runtime_retained *r,bool stopped){
 return r&&stopped&&!r->session.ownership.readers&&!r->session.ownership.writer&&
  (r->session.ownership.owner==SP1_STORE_VALIDATING||r->session.ownership.owner==SP1_STORE_PERFORMANCE);
}
music_media music_backend_mount(music_runtime_retained *r,music_runtime_workspace *w,pool_io raw,bool stopped){
 if(!borrowable(r,stopped)||!w||!raw.read||!raw.write||!raw.sync||!raw.device_blocks)return MUSIC_MEDIA_INVALID;
 /* Reject the old map immediately, even if remount later fails. */
 reject(r);paged_media=false;
 uint8_t *p=w->streaming_batch;
 if(!raw.read(raw.context,0,p,2))return MUSIC_MEDIA_INVALID;
 if(p[0]==0x41&&p[1]==0x33&&p[2]==0x45&&p[3]==0x53){
  if(!raw.read(raw.context,0,p,6)||!legacy523_valid(p,3072,raw.device_blocks))return MUSIC_MEDIA_INVALID;
  return MUSIC_MEDIA_LEGACY;
 }
 /* diskio callbacks must use raw I/O, never the mapped playback API. */
 memset(&w->mount,0,sizeof(w->mount));
 FRESULT mounted=f_mount(&w->mount.fs,"",1);
 FILINFO info;FRESULT a=mounted==FR_OK?f_stat("TAPELOOP/INDEX0.BIN",&info):FR_DISK_ERR,b=mounted==FR_OK?f_stat("TAPELOOP/INDEX1.BIN",&info):FR_DISK_ERR;
 bool paged=a==FR_OK||b==FR_OK;
 bool names_ok=(a==FR_OK||a==FR_NO_FILE||a==FR_NO_PATH)&&(b==FR_OK||b==FR_NO_FILE||b==FR_NO_PATH);
 bool admitted=mounted==FR_OK&&names_ok&&(paged?music_fat_admit_paged:music_fat_admit_workspace)(&w->mount.fs,raw,&r->io,w->mount.scratch,&w->mount.file);
 /* FatFs registers the object even on failed mount: always detach it before
  * returning the shared workspace to catalog or streaming use. */
 bool detached=f_mount(NULL,"",0)==FR_OK;
 if(!admitted||!detached){reject(r);return MUSIC_MEDIA_INVALID;}
 if(paged){
  paged_media=true;cat422_workspace*pw=(void*)w;
  if(!bridge422_open(r,w)||!cat422_controls_load(pw,&pw->device)){paged_media=false;reject(r);return MUSIC_MEDIA_INVALID;}
  music_session_init(&r->session);return MUSIC_MEDIA_POOL;
 }
 if(!pool_open(&w->stopped_song.pool,r->io)||!music_control_verify(&w->stopped_song.pool)){
  reject(r);return MUSIC_MEDIA_INVALID;
 }
 music_session_init(&r->session);return MUSIC_MEDIA_POOL;
}
bool music_backend_controls(music_runtime_retained *r,music_runtime_workspace *w,uint8_t out[3072],bool stopped){
 if(!borrowable(r,stopped)||!w||!out||!r->io.read)return false;
 if(paged_media){cat422_workspace*p=(void*)w;if(!bridge422_open(r,w)||!cat422_controls_load(p,&p->device)){reject(r);return false;}memmove(out,p->controls,3072);return true;}
 pool_store *s=&w->stopped_song.pool;
 if(!pool_open(s,r->io)||!music_control_load(s,out)){reject(r);return false;}
 return true;
}
bool music_backend_song(music_runtime_retained *r,music_runtime_workspace *w,unsigned song,bool stopped){
 if(!borrowable(r,stopped)||!w||!r->io.read||song>=16)return false;
 if(paged_media)return bridge422_load(r,w,song);
 return music_session_load(&r->session,&w->stopped_song.pool,r->io,song,w->stopped_song.loaded);
}
bool music_backend_load(music_runtime_retained *r,music_runtime_workspace *w,unsigned song,bool stopped){
 if(!borrowable(r,stopped)||!w||!r->io.read||song>=16)return false;
 if(paged_media)return bridge422_load(r,w,song);
 pool_store *s=&w->stopped_song.pool;
 if(!pool_open(s,r->io)||!music_control_load(s,w->stopped_song.controls)){
  /* Runtime reload must retain the first I/O fault and the last writer's
   * reservation evidence. Full reject/reset is for mount admission only. */
  memset(r->session.tracks,0,sizeof(r->session.tracks));
  r->session.ownership.owner=SP1_STORE_INVALID;return false;
 }
 return music_session_load_opened(&r->session,s,song,w->stopped_song.loaded);
}
