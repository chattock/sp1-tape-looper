#include "wav.h"
#include <string.h>
#include <limits.h>
#include "../../maintenance419/library_codec.h"
static uint32_t get(const uint8_t*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=n>>(8*i);}
bool cat422_wav_open(cat422_wav*s,cat422_manifest*m,pool_io raw,uint32_t file){
 if(!s)return false;memset(s,0,sizeof *s);uint8_t b[32];
 if(!m||!m->ready||!raw.read||!cat422_manifest_get(m,file,b))return false;
 s->raw=raw;s->manifest=m;s->file=file;s->first=get(b+16);s->blocks=get(b+20);s->frames=get(b+24);s->spb=get(b+28)&511u;
 if((get(b)&65535)>64||(get(b)>>16)>1||!get(b+4)||!get(b+8)||!s->blocks||!s->frames||(s->spb!=280&&s->spb!=496)||
 s->first<raw.pool_first||s->first-raw.pool_first>=raw.pool_blocks||s->blocks>raw.pool_blocks-(s->first-raw.pool_first)||
 s->first>=raw.device_blocks||s->blocks>raw.device_blocks-s->first||s->frames>(uint64_t)s->blocks*s->spb||s->frames>(UINT32_MAX-44u)/4)return false;
 s->bytes=44+s->frames*4;s->ready=true;return true;
}
static bool pcm(cat422_wav*s,uint32_t block){
 if(s->cached&&s->block==block)return true;
 bool sequential=s->cached&&s->block+1==block;
 s->cached=false;
 if(block>=s->blocks)return false;
 if(sequential)memcpy(s->encoded,s->next,512);
 else if(!s->raw.read(s->raw.context,s->first+block,s->encoded,1))return false;
 if(!lc_decode(s->encoded,s->spb,s->pcm))return false;
 if(block+1<s->blocks){
  if(!s->raw.read(s->raw.context,s->first+block+1,s->next,1))return false;
  /* Headerless pads and A7 leave the sequential decoder's interpolation state
   * unchanged. Reconstruct that state only when the next marked block heals. */
  if(s->encoded[11]!=0x5b&&s->next[11]==0x5b){
   int16_t original[2]={s->pcm[(s->spb-2)*2],s->pcm[(s->spb-2)*2+1]};
   int16_t prev[2]={0,0};uint32_t prior=block;
   while(prior){if(!s->raw.read(s->raw.context,s->first+--prior,s->encoded,1))return false;
    if(s->encoded[11]==0x5b){if(s->encoded[2]==0x36)prev[0]=prev[1]=lc_s16(s->encoded+510);else if(s->encoded[2]==0x34){prev[0]=lc_p14(s->encoded,278);prev[1]=lc_p14(s->encoded,279);}break;}}
   s->pcm[(s->spb-2)*2]=prev[0];s->pcm[(s->spb-2)*2+1]=prev[1];
   if(!lc_heal(s->next,s->spb,s->pcm))return false;
   s->pcm[(s->spb-2)*2]=original[0];s->pcm[(s->spb-2)*2+1]=original[1];
  }else if(!lc_heal(s->next,s->spb,s->pcm))return false;
 }
 s->cached=true;s->block=block;return true;
}
bool cat422_wav_read(cat422_wav*s,uint32_t off,void*out,uint32_t len){
 if(!s||!s->ready||!s->manifest->ready||!out||off>s->bytes||len>s->bytes-off)return false;
 uint8_t*b=out,h[44]={0};memcpy(h,"RIFF",4);put(h+4,s->bytes-8);memcpy(h+8,"WAVEfmt ",8);put(h+16,16);h[20]=1;h[22]=2;put(h+24,48000);put(h+28,192000);h[32]=4;h[34]=16;memcpy(h+36,"data",4);put(h+40,s->bytes-44);
 while(len){
  if(off<44){uint32_t n=44-off;if(n>len)n=len;memcpy(b,h+off,n);b+=n;off+=n;len-=n;continue;}
  uint32_t pos=off-44,block=pos/(s->spb*4),within=pos%(s->spb*4),n=s->spb*4-within;if(n>len)n=len;
  if(!pcm(s,block)){s->ready=false;return false;}
#if defined(SP1_MAINTENANCE_CRC504) && defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  /* PCM is already little-endian stereo. Preserve arbitrary/odd byte reads
   * while using the library copy routine rather than reserializing every byte. */
  memcpy(b,(const uint8_t*)s->pcm+within,n);
#else
  for(uint32_t i=0;i<n;i++){uint32_t at=within+i;uint16_t v=(uint16_t)s->pcm[at/2];b[i]=v>>(8*(at&1));}
#endif
  b+=n;off+=n;len-=n;
 }return true;
}
