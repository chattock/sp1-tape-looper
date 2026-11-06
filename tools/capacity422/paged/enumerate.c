#include "enumerate.h"
static uint32_t u32(const uint8_t*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
bool cat422_enumerate(pg422*s,unsigned lane,cat422_emit emit,void*ctx,uint32_t*count){
 cat422_lane l;uint8_t b[32];cat422_extent e;
 if(count)*count=0;
 if(!s||!s->ready||s->transaction||!emit||!count||!cat422_lane_read(s,lane,&l)||l.count>PG422_LIMIT)return false;
 uint64_t generation=s->generation;uint32_t id=l.head,previous=0,found=0,selected=0;
 for(uint32_t n=0;n<l.count;n++){
  if(!id||id<66||!pg422_get(s,id,b)||u32(b)!=4||u32(b+12)!=lane||u32(b+16)!=previous)return false;
  uint32_t next=u32(b+20),audio=u32(b+4),annotation=u32(b+8);
  if(!audio||!cat422_extent_read(s,audio,&e)||(annotation&&!cat422_extent_read(s,annotation,&e)))return false;
  bool current=id==l.selected;
  if(current&&(audio!=l.audio||annotation!=l.annotation))return false;
  if(current){if(found)return false;found=n+1;}
  if(id==l.selected){if(!current)return false;selected=id;}
  cat422_entry entry={lane,n+1,id,audio,annotation,current};
  if(!emit(ctx,&entry)||s->generation!=generation||s->transaction||!s->ready)return false;
  previous=id;id=next;
 }
 if(id||previous!=l.tail||(l.selected&&selected!=l.selected)||(!l.audio&&(l.annotation||l.selected||l.ordinal)))return false;
 if(l.audio&&!found){
  if(l.selected||l.ordinal!=l.count+1||!cat422_extent_read(s,l.audio,&e)||(l.annotation&&!cat422_extent_read(s,l.annotation,&e)))return false;
  cat422_entry entry={lane,l.count+1,0,l.audio,l.annotation,true};
  if(!emit(ctx,&entry)||s->generation!=generation||s->transaction||!s->ready)return false;
  *count=l.count+1;
 }else{if(l.audio&&l.ordinal!=found)return false;*count=l.count;}
 return true;
}
