#include "drive.h"
#include <string.h>
/* Leave substantial journal space for rewrites and filesystem metadata. The
 * host never sees the FAT32 minimum-size padding as usable staging capacity. */
static void editable(drive422*s){s->view.writable=true;uint32_t slots=s->overlay.limit,map_slots=s->overlay.io.buckets*OV_ENTRIES;if(slots>map_slots)slots=map_slots;uint32_t free=slots/128u; /* Half the actual overlay sector budget, in64-sector clusters. Reserve the other half for rewrites/metadata. */
 if(free>4096u)free=4096u; /*128MiB bound; actual overlay reserve is stricter. */
 uint32_t available=s->view.clusters-s->view.used;if(free>available)free=available;
 s->view.unavailable_from=s->view.used+2+free;}
bool drive422_prepare(drive422*s,cat422_workspace*w,pool_io raw,uint32_t first,uint32_t blocks,uint64_t identity,arena422_locked lock,void*gate){
 if(!s||!w)return false;memset(s,0,sizeof *s);
 if(!cat422_banks_open(&w->index,&w->checkpoint,&w->device,raw)||!arena422_create(&s->arena,raw,first,blocks,identity,w->index.generation,lock,gate))return false;
 if(!cat422_directory_build(w,&w->device,&s->files,arena422_manifest_io(&s->arena,0))||!cat422_view_build(&s->view,&s->files,&s->nodes,arena422_manifest_io(&s->arena,1),raw))return false;
 if(!overlay_create(&s->overlay,arena422_overlay_io(&s->arena,s->view.sectors))||!arena422_transition(&s->arena,ARENA422_READY))return false;
 editable(s);s->ready=true;return true;
}
bool drive422_reopen(drive422*s,pool_io raw,uint32_t first,uint32_t blocks,uint64_t identity,uint64_t gen,arena422_locked lock,void*gate){
 if(!s)return false;memset(s,0,sizeof *s);
 if(!arena422_open(&s->arena,raw,first,blocks,identity,gen,lock,gate)||(s->arena.state!=ARENA422_READY&&s->arena.state!=ARENA422_COMMITTING))return false;
 if(!cat422_manifest_open(&s->files,arena422_manifest_io(&s->arena,0),gen)||!cat422_manifest_open(&s->nodes,arena422_manifest_io(&s->arena,1),gen)||!cat422_view_open(&s->view,&s->files,&s->nodes,raw)||!overlay_open(&s->overlay,arena422_overlay_io(&s->arena,s->view.sectors)))return false;
 editable(s);s->ready=true;return true;
}
bool drive422_read(drive422*s,uint32_t at,void*out,uint32_t n){
 if(!s||!s->ready||!out||!n||at>=s->view.sectors||n>s->view.sectors-at)return false;
 uint8_t*p=out;while(n){unsigned batch=n>16?16:n;
  if(batch==1){bool found;if(!overlay_get(&s->overlay,at,p,&found)||(!found&&!cat422_view_read(&s->view,at,p,1)))goto fail;}
  else{uint32_t found;if(!overlay_get_many(&s->overlay,at,p,batch,&found))goto fail;
   for(unsigned i=0;i<batch;i++)if(!(found&(1u<<i))&&!cat422_view_read(&s->view,at+i,p+i*512,1))goto fail;}
  at+=batch;p+=batch*512;n-=batch;
 }return true;
fail:s->ready=false;return false;
}
/* Exclusive READY host writes may borrow admission/converter storage. It is
 * not in use by a decoder until the drive freezes and host ownership ends. */
extern void *wav_work469(size_t*);
static bool base602(void*ctx,uint32_t at,void*out,uint32_t n){return cat422_view_read(ctx,at,out,n);}
/* All firmware callers require admitted, exclusive host ownership. */
#ifdef IMPORT469
__attribute__((section(".conv469.text.drivewrite575")))
#endif
bool drive422_write(drive422*s,uint32_t at,const void*in,uint32_t n){
 if(!s||!s->ready||s->arena.state!=ARENA422_READY||!in||!n||at>=s->view.sectors||n>s->view.sectors-at)return false;
 /* Exclusive host ownership lets comparison reuse the existing overlay
  * verification sector. The overlay writer consumes it only after comparison. */
 const uint8_t*p=in;uint8_t*old=s->overlay.verify;for(;n;n--,at++,p+=512){
  if(n>2&&s->overlay.io.buckets>2){
   size_t available=0;uint8_t*scratch=wav_work469(&available);
   if(scratch&&available>=16384){
    unsigned limit=n>16?16:n;if(limit>s->overlay.io.buckets)limit=s->overlay.io.buckets;
    if(!overlay_update_span(&s->overlay,at,p,limit,scratch,available>UINT32_MAX?UINT32_MAX:(uint32_t)available,base602,&s->view))goto fail;
    n-=limit-1;at+=limit-1;p+=(limit-1)*512;continue;
   }
  }
  if(!drive422_read(s,at,old,1))goto fail;
  if(!memcmp(old,p,512))continue;
  if(n>1&&s->overlay.io.buckets>1){
   if(!drive422_read(s,at+1,old,1))goto fail;
   if(memcmp(old,p+512,512)){
    if(!overlay_put_many(&s->overlay,at,p,2))goto fail;
    n--;at++;p+=512;continue;
   }
  }
  if(!overlay_put(&s->overlay,at,p))goto fail;
 }return true;
fail:s->ready=false;return false;
}
bool drive422_freeze(drive422*s){return s&&s->ready&&s->arena.state==ARENA422_READY&&arena422_transition(&s->arena,ARENA422_COMMITTING);}

static bool current(drive422*s,cat422_workspace*w){return s&&w&&s->arena.ready&&cat422_banks_open(&w->index,&w->checkpoint,&w->device,s->arena.raw);}
bool drive422_finish(drive422*s,cat422_workspace*w,uint64_t generation){
 if(!s||!s->ready||s->arena.state!=ARENA422_COMMITTING||generation<=s->arena.generation||!current(s,w)||w->index.generation!=generation)return false;
 if(!arena422_transition(&s->arena,ARENA422_COMMITTED))return false;s->ready=false;return true;
}
bool drive422_abort(drive422*s,cat422_workspace*w){
 if(!s||(s->arena.state!=ARENA422_READY&&s->arena.state!=ARENA422_PREPARING)||!current(s,w)||w->index.generation!=s->arena.generation)return false;
 if(!arena422_transition(&s->arena,ARENA422_ABORTED))return false;s->ready=false;return true;
}
bool drive422_next(drive422*s,cat422_workspace*w){
 if(!s||(s->arena.state!=ARENA422_COMMITTED&&s->arena.state!=ARENA422_ABORTED)||!current(s,w))return false;
 s->ready=false;
 if(!arena422_renew(&s->arena,w->index.generation)||!cat422_directory_build(w,&w->device,&s->files,arena422_manifest_io(&s->arena,0))||!cat422_view_build(&s->view,&s->files,&s->nodes,arena422_manifest_io(&s->arena,1),s->arena.raw)||!overlay_create(&s->overlay,arena422_overlay_io(&s->arena,s->view.sectors))||!arena422_transition(&s->arena,ARENA422_READY))return false;
 editable(s);s->ready=true;return true;
}
