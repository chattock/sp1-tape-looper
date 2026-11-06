#include "preview.h"
#include <string.h>
static void field(uint32_t words[3],unsigned bit,uint32_t value){unsigned word=bit/32,shift=bit%32;words[word]|=value<<shift;if(shift>15)words[word+1]|=value>>(32-shift);}
static uint32_t get(const uint32_t words[3],unsigned bit){unsigned word=bit/32,shift=bit%32;uint32_t v=words[word]>>shift;if(shift>15)v|=words[word+1]<<(32-shift);return v&131071u;}
static void publish(cat422_preview*p,const uint32_t words[3]){uint32_t seq=__atomic_load_n(&p->sequence,__ATOMIC_RELAXED);__atomic_store_n(&p->sequence,seq|1u,__ATOMIC_SEQ_CST);for(unsigned i=0;i<3;i++)__atomic_store_n(&p->words[i],words[i],__ATOMIC_RELAXED);__atomic_store_n(&p->sequence,(seq+2)&~1u,__ATOMIC_RELEASE);}
static bool cache442_publish(cat422_preview*p,unsigned song,const uint32_t ord[4],unsigned empty,bool counts){if(!p||!ord||song>=16)return false;uint32_t words[3]={0};for(unsigned i=0;i<4;i++){if(ord[i]>=131071)return false;field(words,i*17,ord[i]);}words[2]|=song<<4;words[2]|=1u<<8;words[2]|=(empty&15u)<<9;if(counts)words[2]|=1u<<13;publish(p,words);return true;}
bool cat430_preview_publish(cat422_preview*p,unsigned song,const uint32_t ord[4],unsigned empty){return cache442_publish(p,song,ord,empty,false);}
bool cat442_preview_publish(cat422_preview*p,unsigned song,const uint32_t values[4],unsigned empty){return cache442_publish(p,song,values,empty,true);}
static bool cache442_read(const cat422_preview*p,unsigned song,uint32_t ord[4],bool counts){if(!p||!ord||song>=16)return false;uint32_t seq=__atomic_load_n(&p->sequence,__ATOMIC_ACQUIRE);if(seq&1)return false;uint32_t words[3];for(unsigned i=0;i<3;i++)words[i]=__atomic_load_n(&p->words[i],__ATOMIC_RELAXED);__atomic_thread_fence(__ATOMIC_ACQUIRE);if(seq!=__atomic_load_n(&p->sequence,__ATOMIC_ACQUIRE)||!(words[2]&(1u<<8))||((words[2]>>4)&15)!=song)return false;for(unsigned i=0;i<4;i++)ord[i]=!counts&&(words[2]&(1u<<13))&&(words[2]&(1u<<(9+i)))?0:get(words,i*17);return true;}
bool cat422_preview_read(const cat422_preview*p,unsigned song,uint32_t ord[4]){return cache442_read(p,song,ord,false);}
bool cat442_preview_read(const cat422_preview*p,unsigned song,uint32_t values[4]){return cache442_read(p,song,values,true);}
void cat422_preview_clear(cat422_preview*p){if(p){uint32_t words[3]={0};publish(p,words);}}

bool cat422_preview_publish(cat422_preview*p,unsigned song,const uint32_t ord[4]){return cat430_preview_publish(p,song,ord,0);}
unsigned cat430_preview_empty(const cat422_preview*p,unsigned song){
 if(!p||song>=16)return 0;
 uint32_t seq=__atomic_load_n(&p->sequence,__ATOMIC_ACQUIRE);
 if(seq&1)return 0;
 uint32_t w=__atomic_load_n(&p->words[2],__ATOMIC_RELAXED);
 __atomic_thread_fence(__ATOMIC_ACQUIRE);
 if(seq!=__atomic_load_n(&p->sequence,__ATOMIC_ACQUIRE)||!(w&(1u<<8))||((w>>4)&15u)!=song)return 0;
 return (w>>9)&15u;
}

static void publish443(cat443_preview*p,const uint32_t words[5],unsigned song,bool valid){
 uint32_t seq=__atomic_load_n(&p->header,__ATOMIC_RELAXED)&0xffffc000u;
 __atomic_store_n(&p->header,seq|0x2000u,__ATOMIC_SEQ_CST);
 for(unsigned i=0;i<4;i++)__atomic_store_n(&p->words[i],words[i],__ATOMIC_RELAXED);
 __atomic_store_n(&p->header,((seq+0x4000u)&0xffffc000u)|(words[4]&255u)|(song<<8)|(valid?0x1000u:0),__ATOMIC_RELEASE);
}
bool cat443_preview_publish(cat443_preview*p,unsigned song,const uint32_t ord[4],const uint32_t totals[4]){
 if(!p||!ord||!totals||song>=16)return false;
 uint32_t words[5]={0};
 for(unsigned i=0;i<4;i++){if(totals[i]>=131071u||ord[i]>totals[i])return false;field(words,i*34,ord[i]);field(words,i*34+17,totals[i]);}
 publish443(p,words,song,true);return true;
}
static int read443(const cat443_preview*p,unsigned song,uint32_t values[4],bool totals,uint32_t counts[4]){
 if(!p||song>=16)return -1;
 uint32_t header=__atomic_load_n(&p->header,__ATOMIC_ACQUIRE);
 if((header&0x2000u)||!(header&0x1000u)||((header>>8)&15u)!=song)return -1;
 uint32_t words[5];for(unsigned i=0;i<4;i++)words[i]=__atomic_load_n(&p->words[i],__ATOMIC_RELAXED);words[4]=header&255u;
 __atomic_thread_fence(__ATOMIC_ACQUIRE);
 if(header!=__atomic_load_n(&p->header,__ATOMIC_ACQUIRE))return -1;
 unsigned empty=0;for(unsigned i=0;i<4;i++){uint32_t ordinal=get(words,i*34),count=get(words,i*34+17);if(!ordinal&&count)empty|=1u<<i;if(values)values[i]=totals?count:ordinal;if(counts)counts[i]=count;}
 return (int)empty;
}
bool cat443_preview_read(const cat443_preview*p,unsigned song,uint32_t values[4],bool totals){return values&&read443(p,song,values,totals,0)>=0;}
unsigned cat443_preview_empty(const cat443_preview*p,unsigned song){int value=read443(p,song,0,false,0);return value>=0?(unsigned)value:0u;}
void cat443_preview_clear(cat443_preview*p){if(p){uint32_t words[5]={0};publish443(p,words,0,false);}}

bool cat517_preview_lanes(const cat443_preview*p,unsigned song,uint32_t ord[4],uint32_t totals[4]){return ord&&totals&&read443(p,song,ord,false,totals)>=0;}
