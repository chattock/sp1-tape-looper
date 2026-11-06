#include "native.h"
#include "../commit421/runtime/src/pool.h"
#include <string.h>
#ifdef IMPORT469
#define NATIVE593 __attribute__((section(".conv469.text.native593"),noinline))
#else
#define NATIVE593
#endif
static uint32_t get(const uint8_t*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t wide(const uint8_t*p){return get(p)|((uint64_t)get(p+4)<<32);}
NATIVE593 bool native593_probe(wav_source s,native593_info*out,uint8_t h[512]){
 if(!s.read||!out||!h||s.bytes<1024||!s.read(s.ctx,0,h,512))return false;
 if(memcmp(h,"SP1N593\0",8)||get(h+8)!=1||get(h+12)!=512||get(h+16)!=1)return false;
 native593_info v={.frames=get(h+20),.blocks=get(h+28),.crc=get(h+72),.lane=get(h+32),.action=get(h+36),.ordinal=get(h+40),.generation=wide(h+48),.identity=wide(h+56),.epoch=wide(h+64)};
 if(!v.frames||v.frames>35840u*643u||v.blocks!=(v.frames+279ull)/280||get(h+24)!=(uint64_t)v.blocks*512||s.bytes!=512ull+(uint64_t)v.blocks*512||v.lane>=64||get(h+44)||!v.identity||!v.epoch||!v.generation)return false;
 if(v.action!=2&&v.action!=3)return false;
 if((v.action==3&&v.ordinal)||(v.action==2&&(!v.ordinal||v.ordinal>9999999u)))return false;
 for(unsigned i=80;i<512;i++)if(h[i])return false;
 uint32_t sum=get(h+76);memset(h+76,0,4);if(~pool_crc(UINT32_MAX,h,512)!=sum)return false;
 *out=v;return true;
}
NATIVE593 bool native593_blocks(const uint8_t*p,unsigned n){
 if(!p||!n||n>16)return false;
 for(unsigned j=0;j<n;j++,p+=512){
  if(memcmp(p,"P14S",4)||p[4]||p[5]!=1||p[6]||p[7]||p[8]||p[9]||p[10]||p[11]!=0x5b||p[12]!=24||p[13]!=1||p[14]>3||p[15])return false;
  for(unsigned i=506;i<512;i++)if(p[i])return false;
 }return true;
}
NATIVE593 bool native593_inspect(wav_source s,native593_info*out,uint8_t*scratch,unsigned capacity){
 native593_info v;if(capacity<512||!native593_probe(s,&v,scratch))return false;
 unsigned batch=capacity/512;if(batch>16)batch=16;uint32_t crc=UINT32_MAX;
 for(uint32_t i=0;i<v.blocks;){unsigned n=v.blocks-i;if(n>batch)n=batch;
  if(!s.read(s.ctx,512+i*512,scratch,n*512)||!native593_blocks(scratch,n))return false;
  crc=pool_crc(crc,scratch,n*512);i+=n;
 }
 if(~crc!=v.crc)return false;*out=v;return true;
}
