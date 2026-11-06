#include "editor.h"
#include "../../commit421/runtime/src/controls.h"
#include "../../commit421/runtime/src/snapshot.h"
#include <string.h>
__attribute__((weak)) void eject449_phase(unsigned phase){(void)phase;}
__attribute__((weak)) void editor471_checkpoint(unsigned step){(void)step;}
static uint32_t get(const uint8_t*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static bool verify_controls(cat422_workspace*w,cat422_device*d){
 if(!cat422_controls_load(w,d))return false;bool extended=music_native_snapshot_extended(w->controls);
 for(unsigned song=0;song<16;song++){memset(w->loaded,0,sizeof w->loaded);for(unsigned t=0;t<4;t++){cat422_lane l;pool_object audio;if(!cat422_lane_read(&w->index,song*4+t,&l))return false;if(l.audio&&!cat422_take_load(w,d,song*4+t,w->loaded+t,&audio))return false;}if(!music_native_song_validate_prechecked(w->controls,w->loaded,song,extended))return false;}return true;
}
static bool payload(void*c,uint32_t id,const uint8_t b[32]){(void)id;cat422_workspace*w=c;if(get(b)!=3||!get(b+20))return true;uint32_t at=get(b+4),n=get(b+8),crc=UINT32_MAX;pool_io raw=w->device.raw;if(!n||at>=raw.pool_blocks||n>raw.pool_blocks-at)return false;
 for(uint32_t i=0;i<n;i++){if(!raw.read(raw.context,raw.pool_first+at+i,w->sector,1))return false;crc=pool_crc(crc,w->sector,512);}return ~crc==get(b+12);
}
bool editor422_verify(cat422_workspace*w,pool_io raw,uint64_t generation){return w&&cat422_banks_open(&w->index,&w->checkpoint,&w->device,raw)&&w->index.generation==generation&&verify_controls(w,&w->device)&&pg422_walk(&w->index,payload,w);}
/*512: reserve a complete contiguous batch once. Only the checked tail above
 * ALL reachable live extents in the transaction and both checkpoints qualifies.
 * If it cannot fit, retain the original checked per-file fragmented allocator. */
typedef struct{uint32_t high,pool;} tail512;
static bool tail512_visit(void*c,uint32_t id,const uint8_t b[32]){
 (void)id;tail512*t=c;if(get(b)!=3||!get(b+20))return true;
 uint32_t first=get(b+4),n=get(b+8);if(!n||first>=t->pool||n>t->pool-first)return false;
 if(first+n>t->high)t->high=first+n;return true;
}
#ifdef IMPORT469
__attribute__((noinline,section(".conv469.text.reserve512")))
#endif
static bool reserve512(cat422_workspace*w,cat422_device*d,uint32_t n,uint32_t*cursor,uint32_t*end){
 *cursor=*end=0;if(!n)return true;tail512 t={0,d->raw.pool_blocks};
 if(!pg422_walk(&w->index,tail512_visit,&t))return false;
 for(unsigned i=0;i<2;i++)if(!pg422_checkpoint(&w->checkpoint,w->index.io,i)||!pg422_walk(&w->checkpoint,tail512_visit,&t))return false;
 uint32_t pad=(0u-(d->raw.pool_first+t.high))&15u;
 if(pad>t.pool-t.high||n>t.pool-t.high-pad)return true;
 *cursor=t.high+pad;*end=*cursor+n;return true;
}
static bool stopped(editor422*e,unsigned why){e->error=why;return false;}
bool editor422_commit(editor422*e,drive422*d,cat422_workspace*w){
 if(!e||!d||!w)return false;memset(e,0,sizeof *e);
 bool resume=d->arena.state==ARENA422_COMMITTING;
 if(resume){if(publication422_open(&e->publication,&d->arena)!=1||e->publication.state)return stopped(e,1);}
 else if(!drive422_freeze(d)||!publication422_create(&e->publication,&d->arena))return stopped(e,1);
 editor471_checkpoint(1);e->phase=1;eject449_phase(1);
 uint32_t reserve_blocks=0;
 if(!discover512_build(&e->discovery,d,&e->changes,&reserve_blocks))return stopped(e,1);
 uint32_t source_changes=e->changes;editor471_checkpoint(2);e->phase=2;eject449_phase(4);import422_edit edit;
 for(uint32_t i=0;i<d->files.count;i++){int result=discover422_deleted(&e->discovery,i,&edit);if(result<0)return stopped(e,2);e->changes+=result;}
 if(!e->changes){if(!cat422_banks_open(&w->index,&w->checkpoint,&w->device,d->arena.raw)||w->index.generation!=d->arena.generation||!verify_controls(w,&w->device)||!arena422_transition(&d->arena,ARENA422_COMMITTED))return stopped(e,3);d->ready=false;return true;}
 if(!cat422_banks_open(&w->index,&w->checkpoint,&w->device,publication422_io(&e->publication))||w->index.generation!=d->arena.generation||!verify_controls(w,&w->device)||!cat422_begin(&w->index))return stopped(e,4);
 editor471_checkpoint(3);e->phase=3;eject449_phase(5);uint32_t import_hint=0,reserved_end=0;
 if(!reserve512(w,&w->device,reserve_blocks,&import_hint,&reserved_end))return stopped(e,5);
 if(source_changes&&!(e->discovery.directory_valid&DISCOVER510_GENERAL_WORK)&&overlay509_build(&d->overlay)<0)return stopped(e,5);
 for(uint32_t i=0;i<e->discovery.files.count;i++){
  editor471_checkpoint(1000+i);if(!discover422_get(&e->discovery,i,&edit))return stopped(e,5);if(!edit.action)continue;
  discover422_source source;unsigned input_error=0;
  if(!discover422_source_open(&e->discovery,i,&source,&edit.source))return stopped(e,6);
  if(!import593_stage(w,&w->device,&edit,&import_hint,reserved_end,&input_error)){
   if(input_error){uint8_t row[32];if(!cat422_manifest_get(&e->discovery.files,i,row))return stopped(e,6);memcpy(e->discovery.input_name,row+16,11);e->discovery.input_name[11]=0;e->discovery.input_error=input_error;}
   return stopped(e,6);
  }e->done++;
 }
 for(uint32_t i=0;i<d->files.count;i++){int result=discover422_deleted(&e->discovery,i,&edit);if(result<0||(result&&!import422_stage(w,&w->device,&edit)))return stopped(e,7);e->done+=result;}
 editor471_checkpoint(4);e->phase=4;eject449_phase(6);
 editor471_checkpoint(41);if(!import422_controls(w,&w->device))return stopped(e,8);
 editor471_checkpoint(42);if(!cat422_commit(&w->index))return stopped(e,8);
 editor471_checkpoint(43);if(publication422_open(&e->publication,&d->arena)!=2)return stopped(e,8);
 editor471_checkpoint(44);if(!cat422_banks_open(&w->index,&w->checkpoint,&w->device,d->arena.raw))return stopped(e,8);
 editor471_checkpoint(45);if(!verify_controls(w,&w->device))return stopped(e,8);
 editor471_checkpoint(46);if(!drive422_finish(d,w,d->arena.generation+1))return stopped(e,8);
 return true;
}
bool editor422_recover(editor422*e,drive422*d,cat422_workspace*w){
 if(!e||!d||!w||d->arena.state!=ARENA422_COMMITTING)return false;memset(e,0,sizeof *e);
 editor471_checkpoint(71);unsigned state=publication422_open(&e->publication,&d->arena);if(!state)return stopped(e,9);if(state==1&&!e->publication.state)return editor422_commit(e,d,w);
 editor471_checkpoint(72);if(!cat422_banks_open(&w->index,&w->checkpoint,&w->device,publication422_target_io(&e->publication))||w->index.generation!=d->arena.generation+1)return stopped(e,10);
 editor471_checkpoint(73);if(!verify_controls(w,&w->device))return stopped(e,10);
 editor471_checkpoint(74);if(!pg422_walk(&w->index,payload,w))return stopped(e,10);
 editor471_checkpoint(75);if(state==1&&!publication422_replay(&e->publication))return stopped(e,11);
 editor471_checkpoint(76);if(!cat422_banks_open(&w->index,&w->checkpoint,&w->device,d->arena.raw))return stopped(e,12);
 editor471_checkpoint(77);if(!verify_controls(w,&w->device))return stopped(e,12);
 editor471_checkpoint(78);if(!drive422_finish(d,w,d->arena.generation+1))return stopped(e,12);return true;
}

bool editor422_discard(editor422*e,drive422*d,cat422_workspace*w){
 if(!e||!d||!w)return false;
 if(d->arena.state==ARENA422_READY||d->arena.state==ARENA422_PREPARING){if(!editor422_verify(w,d->arena.raw,d->arena.generation))return false;return drive422_abort(d,w);}
 /* A durable target intent can never be discarded. Only exact original roots
  * plus all referenced payloads authorize this rollback. */
 if(d->arena.state!=ARENA422_COMMITTING||publication422_open(&e->publication,&d->arena)!=1||e->publication.state||!editor422_verify(w,d->arena.raw,d->arena.generation)||!arena422_transition(&d->arena,ARENA422_ABORTED))return false;
 d->ready=false;return true;
}
