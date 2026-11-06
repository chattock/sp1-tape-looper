/* Read-only telemetry/admission for the synthetic host-owned volume. Caller
 * holds media_lock. Never reads/writes musical payload or changes the gate. */
#define SPACE571_FN __attribute__((section(".fresh529.text.space571"),noinline))
static SPACE571_FN void staging571_caps(void){
 static const char text[] __attribute__((section(".fresh529.rodata.space571")))="STORAGE425 CAPS paged422-v1 raw-gated=1 max_blocks=16 drive-paged=1 staging=571 wipe_pending=%u\n";
 const fresh529_request*r=(const void*)FRESH529_ADDRESS;reply(text,fresh529_authorized(r)&&fresh529_pending(r));
}
static SPACE571_FN bool staging571_space(uint8_t work[512],uint32_t*out_capacity,uint32_t*out_free){
 if(!paged_drive.ready||paged_drive.arena.state!=ARENA422_READY||host_failed)return false;
 cat422_view*v=&paged_drive.view;overlay*o=&paged_drive.overlay;
 if(v->unavailable_from<v->used+2||o->next>o->limit)return false;
 uint32_t first=v->used+2,last=v->unavailable_from,free=0;
 *out_capacity=(last-first)*32768u;
 /*575: a current free-space scan, with bounded span reads in the admitted
  * exclusive host phase. Borrow existing decoder scratch; never cache space. */
 size_t available=0;uint8_t*scan=NULL;
 if(auto_mode&&auto_phase()==2&&host_active)scan=wav_work469(&available);
 unsigned batch=scan&&available>=8192?16:1;if(batch==1)scan=work;
 for(uint32_t c=first;c<last;){
  uint32_t page=c/128,pages=(last-1)/128-page+1;if(pages>batch)pages=batch;
  if(!drive422_read(&paged_drive,32+page,scan,pages))return false;
  uint32_t end=(page+pages)*128;if(end>last)end=last;
  for(;c<end;c++)if(!(get32(scan+(c-page*128)*4)&0x0fffffffu))free++;
 }
 uint32_t map=o->io.buckets*OV_ENTRIES,slots=o->limit;if(slots>map)slots=map;
 uint32_t remaining=slots>o->next?(slots-o->next)/128u:0;
 if(free>remaining)free=remaining;
 *out_free=free*32768u;return true;
}
