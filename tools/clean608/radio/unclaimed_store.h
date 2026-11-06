#pragma once
/* Research predicate only. Not linked into setup333 or any released firmware.
 * Caller must independently prove hardware/static layout, erased backup VS and
 * release target. This predicate NEVER grants write authority by itself.
 */
#include <stdbool.h>
#include <stdint.h>
static bool review_unclaimed_store(const uint8_t vs[4096],unsigned *free_bytes){
 bool seen[124]={false};unsigned at=0;
 while(at+4<=4096){
  unsigned tag=vs[at],n=vs[at+1],check=vs[at+2],hdr=vs[at+3];
  if(tag==255){
   for(unsigned i=at;i<4096;i++)if(vs[i]!=255)return false;
   if(at+736>=4093)return false; /* Initial empty records + one pair of updates. */
   *free_bytes=4096-at;return true;
  }
  unsigned next=at+4+((n+1)&~1u);if(!n||next>4096)return false;
  unsigned sum=0;for(unsigned i=0;i<n;i++)sum+=vs[at+4+i];
  if(((sum^0x55)&255)!=check)return false;
  if(tag){
   unsigned id=tag&127;
   if(!(tag&128)||id<1||id>0x7b||seen[id]||(((tag+n+check)^0x55)&255)!=hdr||id==0x70||id==0x71)return false;
   seen[id]=true;
  }
  at=next;
 }
 return false;
}
