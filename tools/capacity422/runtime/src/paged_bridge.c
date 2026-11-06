#include "paged_bridge.h"
#include "fault.h"
#include <string.h>
__attribute__((weak)) bool storage422_transport_idle(void){return false;}
static cat422_workspace* workspace(music_runtime_workspace*w){return (cat422_workspace*)(void*)w;}
static bool failed(music_runtime_retained*r){music_fault_fail(r,0);r->session.ownership.owner=SP1_STORE_INVALID;return false;}
/* Opening/validating an index never implies permission to maintain it. Boot
 * preflight uses this read-only path even after song loading sets PERFORMANCE. */
static bool open441_readonly(music_runtime_retained*r,music_runtime_workspace*w){
 if(r)music_prepared427_revoke(&r->session);
 cat422_workspace*p=workspace(w);if(!r||!w||!cat422_banks_open(&p->index,&p->checkpoint,&p->device,r->io))return false;
 return true;
}
bool bridge422_open(music_runtime_retained*r,music_runtime_workspace*w){
 if(!open441_readonly(r,w))return false;
 cat422_workspace*p=workspace(w);
 /* No compaction while a writer reserves payload space or audio owns maps.
  * Keep an ample append reserve for later performance-only transactions. */
 if(p->index.tail>4096&&r->session.ownership.owner==SP1_STORE_PERFORMANCE&&!r->session.ownership.writer&&!r->session.ownership.readers&&storage422_transport_idle())return cat422_banks_compact(&p->index,&p->checkpoint,&p->device,true);
 return true;
}
bool bridge422_load(music_runtime_retained*r,music_runtime_workspace*w,unsigned song){cat422_workspace*p=workspace(w);return bridge422_open(r,w)&&cat439_session_load_open(&r->session,p,&p->device,song);}
bool bridge422_preflight(music_runtime_retained*r,music_runtime_workspace*w){cat422_workspace*p=workspace(w);return open441_readonly(r,w)&&cat422_session_preflight(&r->session,p,&p->device);}
bool bridge422_prepare(music_runtime_retained*r,music_runtime_workspace*w,unsigned track,uint32_t capacity,bool clone){
 if(!bridge422_load(r,w,r->session.song))return failed(r);
 return bridge426_prepare_loaded(r,w,track,capacity,clone);
}
bool bridge426_prepare_loaded(music_runtime_retained*r,music_runtime_workspace*w,unsigned track,uint32_t capacity,bool clone){
 cat422_workspace*p=workspace(w);
 if(!r||!w||track>=4||!p->index.ready||p->index.transaction||r->session.ownership.owner!=SP1_STORE_PERFORMANCE||r->session.ownership.writer||r->session.ownership.readers)return false;
 music_fault_step(r,MUSIC_PREP_RESERVE);bool ok=cat422_session_prepare(&r->session,p,&p->device,track,capacity,clone);
 if(!(r->session.writer.fault&255u))r->session.writer.fault|=MUSIC_PREP_RESERVE;
 if(!ok&&!p->index.ready)return failed(r);return ok;
}
bool bridge422_ready(music_runtime_retained*r,music_runtime_workspace*w){cat422_workspace*p=workspace(w);return bridge422_open(r,w)&&cat422_session_edit_ready(&r->session,p,&p->device);}
static bool capture(cat422_workspace*p,music_control_snapshot snapshot,void*ctx){for(unsigned i=0;i<6;i++)if(!snapshot(ctx,i,p->controls+i*512))return false;return true;}
bool bridge422_finish(music_runtime_retained*r,music_runtime_workspace*w,const take_metadata*m,music_control_snapshot snapshot,void*ctx){cat422_workspace*p=workspace(w);if(!bridge422_open(r,w)||!capture(p,snapshot,ctx)||!cat422_session_finish(&r->session,p,&p->device,m))return failed(r);return true;}
bool bridge422_save(music_runtime_retained*r,music_runtime_workspace*w,music_control_snapshot snapshot,void*ctx){cat422_workspace*p=workspace(w);if(!bridge422_open(r,w)||!capture(p,snapshot,ctx)||!cat422_session_save(&r->session,p,&p->device))return failed(r);return true;}
int bridge422_cycle(music_runtime_retained*r,music_runtime_workspace*w,unsigned track,int direction,unsigned*ordinal){
 if(track>=4||(direction!=-1&&direction!=1)||!bridge422_load(r,w,r->session.song))return -1;
 cat422_workspace*p=workspace(w);sp1_extent previous=r->session.tracks[track];
 if(!cat422_session_select(&r->session,p,&p->device,track,direction)){if(!p->index.ready)failed(r);return -1;}
 cat422_lane lane;if(!cat422_lane_read(&p->index,r->session.song*4+track,&lane)){failed(r);return -1;}
 /* The maintained audio fade/drain handshake owns mapping publication. */
 memcpy(p->sector,&r->session.tracks[track],sizeof(sp1_extent));uint32_t lane_id=r->session.song*4+track;memcpy(p->sector+8,&lane_id,4);r->session.tracks[track]=previous;if(ordinal)*ordinal=lane.ordinal;return lane.audio?0:64;
}
bool bridge422_drop(music_runtime_retained*r,music_runtime_workspace*w,unsigned ref){cat422_workspace*p=workspace(w);if(ref>=64||ref/4!=r->session.song||!bridge422_load(r,w,r->session.song))return false;return cat422_session_select(&r->session,p,&p->device,ref%4,0);}
bool bridge422_publish(music_runtime_retained*r,music_runtime_workspace*w,unsigned track){
 cat422_workspace*p=workspace(w);sp1_extent next;uint32_t lane;
 memcpy(&next,p->sector,sizeof next);memcpy(&lane,p->sector+8,4);
 if(track>=4||lane!=r->session.song*4+track)return false;
 r->session.tracks[track]=next;return true;
}

bool bridge427_warm(music_runtime_retained*r,music_runtime_workspace*w,uint32_t capacity){
 if(!r||!w||r->session.writer_phase||r->session.ownership.writer||r->session.ownership.readers||r->session.ownership.owner!=SP1_STORE_PERFORMANCE)return false;
 cat422_workspace*p=workspace(w);
 if(!bridge422_open(r,w))return failed(r);
 bool ok=cat427_session_warm(&r->session,p,&p->device,capacity);
 if(!ok&&(!p->index.ready||r->session.writer.failed))return failed(r);
 return ok;
}

#include "predict437.h"
#include "controls.h"
static uint32_t word437(const uint8_t*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
/* Returns -2 for a valid but not predictable source (use durable path).
 * No writes occur here. Existing roots pin both outgoing and predicted takes. */
int bridge437_predict(music_runtime_retained*r,music_runtime_workspace*w,unsigned track,int direction,switch437_ticket*t){
 if(!t||track>=4||(direction!=-1&&direction!=1)||!bridge422_load(r,w,r->session.song))return -1;
 cat422_workspace*p=workspace(w);cat422_lane l;unsigned lane=r->session.song*4+track;
 if(!cat422_lane_read(&p->index,lane,&l))return -1;
 bool clean440=true,mute_clean440=true;
 if(l.audio){
  const take_metadata *old=p->loaded+track;take_metadata edited=*old;
  if(!music_controls_capture(p->controls,r->session.song,track,&edited))return -1;
  mute_clean440=old->muted==edited.muted;
  clean440=old->track_head_blocks==edited.track_head_blocks&&old->track_tail_blocks==edited.track_tail_blocks&&old->pan==edited.pan&&old->nudge==edited.nudge&&old->content_flags==edited.content_flags&&old->magnet_cells==edited.magnet_cells&&!memcmp(old->magnet,edited.magnet,32);
 }
 /* A zero song clock can be established by the first recalled take. Keep
  * that first handoff sequential, so skipping positions cannot pick a different
  * clock. A live mute override also retains ordinary recall semantics. */
 bool fast440=l.count&&clean440&&mute_clean440&&(!l.audio||l.selected)&&word437(p->controls+8+r->session.song*44+4);
 uint32_t id=0,ordinal=0;uint8_t node[32];sp1_extent extent={0};
 if(fast440&&l.audio&&(!pg422_get(&p->index,l.selected,node)||word437(node)!=4||word437(node+12)!=lane||word437(node+4)!=l.audio||word437(node+8)!=l.annotation))return -1;
 if(direction<0){
  if(!l.count&&!l.audio)return -2;
  if(l.audio){
   /* Dirty/unarchived versions require preservation before prediction. */

   if(!clean440)return -2;
   if(l.selected){
    if(!pg422_get(&p->index,l.selected,node)||word437(node)!=4||word437(node+12)!=lane||word437(node+4)!=l.audio||word437(node+8)!=l.annotation)return -1;
    id=word437(node+16);ordinal=id?l.ordinal-1:l.count;if(!id)id=l.tail;
   }else{
    /* Match archive's duplicate rule without creating an entry. A freshly
     * recorded take would append at the tail, then DOWN selects the old tail. */
    uint32_t at=l.head,prev=0;bool found=false;
    if(l.count>PG422_LIMIT)return -1;
    for(uint32_t n=0;n<l.count;n++){
     if(!at||!pg422_get(&p->index,at,node)||word437(node)!=4||word437(node+12)!=lane||word437(node+16)!=prev)return -1;
     if(word437(node+4)==l.audio&&word437(node+8)==l.annotation){id=prev?prev:l.tail;ordinal=prev?n:l.count;found=true;break;}
     prev=at;at=word437(node+20);
    }
    if(!found){if(at)return -1;id=l.tail;ordinal=l.count?l.count:1;}
   }
  }else{id=l.tail;ordinal=l.count;}
  if(id){if(!pg422_get(&p->index,id,node)||word437(node)!=4||word437(node+12)!=lane)return -1;}
  else { /* Only the just-recorded take exists: predict its archived self. */
   if(!l.audio||l.count)return -1;
   memset(node,0,sizeof node);memcpy(node+4,&l.audio,4);memcpy(node+8,&l.annotation,4);
  }
  pool_object audio;
  if(!cat422_take_objects(p,&p->device,word437(node+4),word437(node+8),p->loaded+track,&audio))return -1;
  extent=(sp1_extent){audio.first,audio.blocks};
  if(!music_controls_track(p->controls,r->session.song,track,p->loaded+track))return -1;
 }else{
  /* New is predictable, but preserve/commit must finish before recording. */
  memset(p->loaded+track,0,sizeof p->loaded[track]);
  if(!music_controls_track(p->controls,r->session.song,track,0))return -1;
 }
 *t=(switch437_ticket){p->index.generation,extent,r->session.song,track,ordinal,direction,extent.count?0:64,id,fast440,false};
 t->unchanged440=t->fast440&&t->selected==l.selected;
 memcpy(p->sector,&extent,sizeof extent);memcpy(p->sector+8,&lane,4);
 return t->result;
}
bool bridge437_finish(music_runtime_retained*r,music_runtime_workspace*w,const switch437_ticket*t){
 cat422_workspace*p=workspace(w);
 if(!t||t->track>=4||r->session.song!=t->song||!p->index.ready||p->index.transaction||p->index.generation!=t->generation||r->session.tracks[t->track].first!=t->extent.first||r->session.tracks[t->track].count!=t->extent.count)return false;
 /* Keep the audible mapping throughout cooperative I/O. Reload only the old
  * durable controls for the existing selection transaction, never old maps. */
 if(!cat422_controls_load(p,&p->device))return false;
 bool ok=t->fast440?cat440_session_select_open(&r->session,p,&p->device,t->track,t->selected,t->ordinal,t->generation):cat422_session_select(&r->session,p,&p->device,t->track,t->direction);
 if(!ok)return false;
 cat422_lane l;
 return r->session.tracks[t->track].first==t->extent.first&&r->session.tracks[t->track].count==t->extent.count&&cat422_lane_read(&p->index,t->song*4+t->track,&l)&&l.ordinal==t->ordinal;
}

/* Resolve a burst against one pinned catalog generation. Each accepted DOWN
 * advances once, UP selects New; only the final target is loaded/published.
 * Never extend a dirty/unarchived source: its ordinary preservation goes first. */
bool bridge440_extend(music_runtime_retained*r,music_runtime_workspace*w,switch437_ticket*t,uint32_t actions,unsigned count){
 cat422_workspace*p=workspace(w);cat422_lane l;uint8_t node[32];
 if(!t||!t->fast440||!count||count>8||t->track>=4||t->song!=r->session.song||!p->index.ready||p->index.transaction||p->index.generation!=t->generation)return false;
 unsigned lane=t->song*4+t->track;
 if(!cat422_lane_read(&p->index,lane,&l))return false;
 uint32_t id=t->selected,ordinal=t->ordinal;
 for(unsigned n=0;n<count;n++,actions>>=2){
  unsigned op=actions&3u;if(op==2){id=ordinal=0;continue;}if(op!=1||!l.count)return false;
  if(id){
   if(!pg422_get(&p->index,id,node)||word437(node)!=4||word437(node+12)!=lane||!ordinal||ordinal>l.count)return false;
   id=word437(node+16);ordinal=id?ordinal-1:l.count;
  }else ordinal=l.count;
  if(!id)id=l.tail;
 }
 sp1_extent extent={0};
 if(id){
  pool_object audio;
  if(!pg422_get(&p->index,id,node)||word437(node)!=4||word437(node+12)!=lane||!cat422_take_objects(p,&p->device,word437(node+4),word437(node+8),p->loaded+t->track,&audio))return false;
  extent=(sp1_extent){audio.first,audio.blocks};
  if(!music_controls_track(p->controls,t->song,t->track,p->loaded+t->track))return false;
 }else{
  memset(p->loaded+t->track,0,sizeof p->loaded[t->track]);
  if(!music_controls_track(p->controls,t->song,t->track,0))return false;
 }
 t->selected=id;t->ordinal=ordinal;t->extent=extent;t->result=extent.count?0:64;t->unchanged440=id==l.selected;
 memcpy(p->sector,&extent,sizeof extent);memcpy(p->sector+8,&lane,4);
 return true;
}
