#include "catalog.h"
#include <string.h>
static uint32_t get(const uint8_t*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint8_t*p,uint32_t x){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(x>>(8*i));}
static bool typed(pg422*s,uint32_t id,unsigned type,uint8_t b[32]){return pg422_get(s,id,b)&&get(b)==type;}
bool cat422_lane_read(pg422*s,unsigned lane,cat422_lane*out){uint8_t b[32];if(!out||lane>=65||!typed(s,lane+1,2,b))return false;*out=(cat422_lane){get(b+4),get(b+8),get(b+12),get(b+16),get(b+20),get(b+24),get(b+28)};return true;}
static bool lane_write(pg422*s,unsigned lane,const cat422_lane*l){uint8_t b[32]={0};put(b,2);put(b+4,l->audio);put(b+8,l->annotation);put(b+12,l->selected);put(b+16,l->head);put(b+20,l->tail);put(b+24,l->count);put(b+28,l->ordinal);return pg422_stage(s,lane+1,b);}
bool cat422_extent_read(pg422*s,uint32_t id,cat422_extent*out){uint8_t b[32],h[32];if(!out||!typed(s,id,3,b)||!typed(s,0,1,h))return false;*out=(cat422_extent){get(b+4),get(b+8),get(b+12),get(b+16)};return out->blocks&&out->first<get(h+16)&&out->blocks<=get(h+16)-out->first;}
bool cat422_begin(pg422*s){uint8_t h[32];return typed(s,0,1,h)&&get(h+4)==1&&pg422_begin(s);}
static bool allocate(pg422*s,uint32_t*id){
 uint8_t h[32],free_record[32];if(!typed(s,0,1,h))return false;
 uint32_t free_id=get(h+28);
 if(free_id){if(free_id<66||!pg422_get(s,free_id,free_record))return false;
  unsigned type=get(free_record);if(type!=5&&(type!=3||get(free_record+20)||get(free_record+24)))return false;
  *id=free_id;put(h+28,get(free_record+28));
 }else{if(get(h+8)<258||get(h+8)>=PG422_LIMIT)return false;*id=get(h+8);put(h+8,*id+1);}
 return pg422_stage(s,0,h);
}
static bool release_id(pg422*s,uint32_t id,uint8_t b[32]){
 /* IDs in the migration's object range can be reused too; lanes cannot. */
 uint8_t h[32];if(id<66||!typed(s,0,1,h))return false;
 put(b+28,get(h+28));put(h+28,id);
 return pg422_stage(s,id,b)&&pg422_stage(s,0,h);
}
static bool do_add_extent(pg422*s,cat422_extent e,uint32_t*id){uint8_t h[32],b[32]={0};if(!id||!typed(s,0,1,h)||!e.blocks||e.first>=get(h+16)||e.blocks>get(h+16)-e.first||!allocate(s,id))return false;put(b,3);put(b+4,e.first);put(b+8,e.blocks);put(b+12,e.crc);put(b+16,e.tag);put(b+24,1);if(!pg422_stage(s,*id,b)||!typed(s,0,1,h)||get(h+24)==UINT32_MAX)return false;put(h+24,get(h+24)+1);return pg422_stage(s,0,h);}
/* Reference accounting lets the allocator skip dead descriptors without
 * scanning all lanes. The previous committed root still pins old extents. */
static bool ref(pg422*s,uint32_t id,int delta){
 if(!id)return true;uint8_t b[32];if(!typed(s,id,3,b))return false;uint32_t n=get(b+20);
 if((delta<0&&!n)||(delta>0&&n==UINT32_MAX))return false;
 /* A zero-reference descriptor is usable only while freshly reserved. Dead
  * descriptors on the free list must never be resurrected by stale IDs. */
 if(delta>0&&!n){if(!get(b+24))return false;uint8_t h[32];if(!typed(s,0,1,h)||!get(h+24))return false;put(h+24,get(h+24)-1);if(!pg422_stage(s,0,h))return false;}
 put(b+20,delta>0?n+1:n-1);put(b+24,0);
 return delta<0&&n==1?release_id(s,id,b):pg422_stage(s,id,b);
}
static bool switch_live(pg422*s,cat422_lane*l,uint32_t audio,uint32_t annotation){cat422_extent e;if(audio&&!cat422_extent_read(s,audio,&e))return false;if(annotation&&(!audio||!cat422_extent_read(s,annotation,&e)||e.blocks!=1))return false;if(audio!=l->audio&&(!ref(s,audio,1)||!ref(s,l->audio,-1)))return false;if(annotation!=l->annotation&&(!ref(s,annotation,1)||!ref(s,l->annotation,-1)))return false;l->audio=audio;l->annotation=annotation;return true;}
static bool archive(pg422*s,unsigned lane,cat422_lane*l){
 if(!l->audio)return true;
 uint32_t id=l->head;uint8_t b[32];
 if(l->selected){if(!typed(s,l->selected,4,b)||get(b+12)!=lane)return false;if(get(b+4)==l->audio&&get(b+8)==l->annotation)return true;}
 if(!typed(s,l->audio,3,b))return false;
 bool unique=get(b+20)==1;
 if(unique)id=0;
 for(uint32_t n=0;n<l->count&&!unique;n++){
  if(!id||!typed(s,id,4,b)||get(b+12)!=lane)return false;
  if(get(b+4)==l->audio&&get(b+8)==l->annotation){l->selected=id;l->ordinal=n+1;return true;}
  id=get(b+20);
 }
 if(id||l->count==UINT32_MAX||!allocate(s,&id))return false;
 memset(b,0,32);put(b,4);put(b+4,l->audio);put(b+8,l->annotation);put(b+12,lane);put(b+16,l->tail);
 if(!pg422_stage(s,id,b)||!ref(s,l->audio,1)||!ref(s,l->annotation,1))return false;
 if(l->tail){if(!typed(s,l->tail,4,b)||get(b+20))return false;put(b+20,id);if(!pg422_stage(s,l->tail,b))return false;}else l->head=id;
 l->tail=id;l->selected=id;l->count++;l->ordinal=l->count;return true;
}
static bool do_new_take(pg422*s,unsigned lane){cat422_lane l;if(lane>=64||!cat422_lane_read(s,lane,&l)||!archive(s,lane,&l)||!switch_live(s,&l,0,0))return false;l.selected=0;l.ordinal=0;return lane_write(s,lane,&l);}
static bool do_preserve(pg422*s,unsigned lane){cat422_lane l;return lane<64&&cat422_lane_read(s,lane,&l)&&archive(s,lane,&l)&&lane_write(s,lane,&l);}
static bool do_cycle(pg422*s,unsigned lane){cat422_lane l;uint8_t b[32];if(lane>=64||!cat422_lane_read(s,lane,&l)||!archive(s,lane,&l)||!l.count)return false;uint32_t next=l.tail,ordinal=l.count;if(l.selected){if(!typed(s,l.selected,4,b))return false;next=get(b+16);if(next)ordinal=l.ordinal-1;else next=l.tail;}if(!typed(s,next,4,b)||get(b+12)!=lane||!switch_live(s,&l,get(b+4),get(b+8)))return false;l.selected=next;l.ordinal=ordinal;return lane_write(s,lane,&l);}
static bool do_recorded(pg422*s,unsigned lane,uint32_t audio,uint32_t annotation){cat422_lane l;if(lane>=64||!audio||!cat422_lane_read(s,lane,&l)||!archive(s,lane,&l)||!switch_live(s,&l,audio,annotation))return false;l.selected=0;l.ordinal=l.count+1;return lane_write(s,lane,&l);}
static bool do_annotated(pg422*s,unsigned lane,uint32_t annotation){
 cat422_lane l;if(lane>=64||!annotation||!cat422_lane_read(s,lane,&l)||!l.audio)return false;
 if(annotation==l.annotation)return true;
 if(!switch_live(s,&l,l.audio,annotation))return false;
 l.selected=0;l.ordinal=l.count+1;return lane_write(s,lane,&l);
}
static bool do_delete(pg422*s,unsigned lane){cat422_lane l;uint8_t b[32],neighbor[32];if(lane>=64||!cat422_lane_read(s,lane,&l)||!l.audio)return false;
 if(l.selected){if(!typed(s,l.selected,4,b)||get(b+12)!=lane||get(b+4)!=l.audio||get(b+8)!=l.annotation||!l.count)return false;uint32_t prev=get(b+16),next=get(b+20);if(prev){if(!typed(s,prev,4,neighbor)||get(neighbor+20)!=l.selected)return false;put(neighbor+20,next);if(!pg422_stage(s,prev,neighbor))return false;}else l.head=next;if(next){if(!typed(s,next,4,neighbor)||get(neighbor+16)!=l.selected)return false;put(neighbor+16,prev);if(!pg422_stage(s,next,neighbor))return false;}else l.tail=prev;if(!ref(s,l.audio,-1)||!ref(s,l.annotation,-1))return false;memset(b,0,32);put(b,5);if(!release_id(s,l.selected,b))return false;l.count--;}
 if(!switch_live(s,&l,0,0))return false;l.selected=0;l.ordinal=0;return lane_write(s,lane,&l);
}
static bool do_control(pg422*s,uint32_t id){uint8_t h[32];cat422_extent e;if(!id||!cat422_extent_read(s,id,&e)||!typed(s,0,1,h))return false;uint32_t old=get(h+12);if(old!=id&&(!ref(s,id,1)||!ref(s,old,-1)))return false;if(!typed(s,0,1,h))return false;put(h+12,id);return pg422_stage(s,0,h);}
bool cat422_commit(pg422*s){uint8_t h[32];if(!s||!typed(s,0,1,h))return false;if(get(h+24)){s->ready=false;return false;}return pg422_commit(s);}

static bool result(pg422*s,bool ok){if(!ok&&s&&s->transaction)s->ready=false;return ok;}
bool cat422_new_take(pg422*s,unsigned lane){return result(s,do_new_take(s,lane));}
bool cat422_cycle(pg422*s,unsigned lane){return result(s,do_cycle(s,lane));}
bool cat440_select(pg422*s,unsigned lane,uint32_t selected,uint32_t ordinal){
 cat422_lane l;uint8_t node[32];
 bool ok=s&&s->transaction&&lane<64&&cat422_lane_read(s,lane,&l);
 /* Never bypass preservation of a new recording or edited annotation. */
 if(ok&&l.audio)ok=l.selected&&typed(s,l.selected,4,node)&&get(node+12)==lane&&get(node+4)==l.audio&&get(node+8)==l.annotation;
 if(ok&&selected)ok=ordinal&&ordinal<=l.count&&typed(s,selected,4,node)&&get(node+12)==lane;
 if(ok&&!selected)ok=!ordinal;
 if(ok)ok=switch_live(s,&l,selected?get(node+4):0,selected?get(node+8):0);
 if(ok){l.selected=selected;l.ordinal=ordinal;ok=lane_write(s,lane,&l);}
 return result(s,ok);
}
bool cat422_delete(pg422*s,unsigned lane){return result(s,do_delete(s,lane));}
bool cat422_recorded(pg422*s,unsigned lane,uint32_t a,uint32_t n){return result(s,do_recorded(s,lane,a,n));}
bool cat422_annotated(pg422*s,unsigned lane,uint32_t n){return result(s,do_annotated(s,lane,n));}
bool cat422_preserve(pg422*s,unsigned lane){return result(s,do_preserve(s,lane));}
bool cat422_control(pg422*s,uint32_t id){return result(s,do_control(s,id));}
bool cat422_add_extent(pg422*s,cat422_extent e,uint32_t*id){return result(s,do_add_extent(s,e,id));}

/* Locate by durable ID, not a transient directory ordinal. Link traversal also
 * proves that the target belongs to this lane before any staged mutation. */
static bool locate_archive(pg422*s,unsigned lane,uint32_t id,cat422_lane*l,uint8_t b[32],uint32_t*ordinal){
 if(!s||!s->transaction||lane>=65||!id||!cat422_lane_read(s,lane,l))return false;
 uint32_t at=l->head,previous=0;if(l->count>PG422_LIMIT)return false;
 for(uint32_t n=0;n<l->count;n++){
  if(!at||!typed(s,at,4,b)||get(b+12)!=lane||get(b+16)!=previous)return false;
  if(at==id){*ordinal=n+1;return true;}previous=at;at=get(b+20);
 }return false;
}
static bool replace_archive(pg422*s,unsigned lane,uint32_t id,uint32_t audio,uint32_t annotation){
 cat422_lane l;uint8_t b[32];uint32_t ordinal;cat422_extent e;
 if(!audio||!cat422_extent_read(s,audio,&e)||(annotation&&!cat422_extent_read(s,annotation,&e))||!locate_archive(s,lane,id,&l,b,&ordinal))return false;
 uint32_t old=get(b+4),old_annotation=get(b+8);
 if(audio==old&&annotation==old_annotation)return true;
 if(!ref(s,audio,1)||!ref(s,annotation,1)||!ref(s,old,-1)||!ref(s,old_annotation,-1))return false;
 put(b+4,audio);put(b+8,annotation);if(!pg422_stage(s,id,b))return false;
 if(l.selected==id){if(!switch_live(s,&l,audio,annotation))return false;l.ordinal=ordinal;return lane_write(s,lane,&l);}
 return true;
}
static bool delete_archive(pg422*s,unsigned lane,uint32_t id){
 cat422_lane l;uint8_t b[32],neighbor[32];uint32_t ordinal;
 if(!locate_archive(s,lane,id,&l,b,&ordinal))return false;
 if(l.selected==id)return lane<64&&do_delete(s,lane);
 uint32_t prev=get(b+16),next=get(b+20);
 if(prev){if(!typed(s,prev,4,neighbor)||get(neighbor+20)!=id||get(neighbor+12)!=lane)return false;put(neighbor+20,next);if(!pg422_stage(s,prev,neighbor))return false;}else l.head=next;
 if(next){if(!typed(s,next,4,neighbor)||get(neighbor+16)!=id||get(neighbor+12)!=lane)return false;put(neighbor+16,prev);if(!pg422_stage(s,next,neighbor))return false;}else l.tail=prev;
 if(!ref(s,get(b+4),-1)||!ref(s,get(b+8),-1))return false;
 memset(b,0,32);put(b,5);if(!release_id(s,id,b))return false;l.count--;
 if(l.audio){if(!l.selected)l.ordinal=l.count+1;else if(l.ordinal>ordinal)l.ordinal--;}
 return lane_write(s,lane,&l);
}
bool cat422_archive_replace(pg422*s,unsigned lane,uint32_t id,uint32_t a,uint32_t n){return result(s,replace_archive(s,lane,id,a,n));}
bool cat422_archive_delete(pg422*s,unsigned lane,uint32_t id){return result(s,delete_archive(s,lane,id));}
static bool add_archive(pg422*s,unsigned lane,uint32_t audio,uint32_t annotation){
 cat422_lane l;uint8_t b[32]={0};uint32_t id;cat422_extent e;
 if(lane>=65||!audio||!cat422_extent_read(s,audio,&e)||(annotation&&!cat422_extent_read(s,annotation,&e))||!cat422_lane_read(s,lane,&l)||l.count>=PG422_LIMIT||!allocate(s,&id))return false;
 put(b,4);put(b+4,audio);put(b+8,annotation);put(b+12,lane);put(b+16,l.tail);
 if(!pg422_stage(s,id,b)||!ref(s,audio,1)||!ref(s,annotation,1))return false;
 if(l.tail){if(!typed(s,l.tail,4,b)||get(b+12)!=lane||get(b+20))return false;put(b+20,id);if(!pg422_stage(s,l.tail,b))return false;}else l.head=id;
 l.tail=id;l.count++;
 if(!l.audio&&lane<64){if(!switch_live(s,&l,audio,annotation))return false;l.selected=id;l.ordinal=l.count;}
 else if(l.audio&&!l.selected)l.ordinal=l.count+1;
 return lane_write(s,lane,&l);
}
bool cat422_archive_add(pg422*s,unsigned lane,uint32_t a,uint32_t n){return result(s,add_archive(s,lane,a,n));}

bool cat422_live_replace(pg422*s,unsigned lane,uint32_t audio,uint32_t annotation){
 cat422_lane l;
 bool ok=lane<64&&audio&&cat422_lane_read(s,lane,&l)&&l.audio&&!l.selected;
 if(ok){ok=switch_live(s,&l,audio,annotation);l.ordinal=l.count+1;if(ok)ok=lane_write(s,lane,&l);}
 return result(s,ok);
}
