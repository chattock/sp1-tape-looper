#include "pack.h"
#include <string.h>
#if defined(__arm__) && defined(__ARM_FEATURE_SAT)
#include <arm_acle.h>
#endif
static int clip591(int v){
#if defined(__arm__) && defined(__ARM_FEATURE_SAT)
 return __ssat(v,16);
#else
 return v>32767?32767:v< -32768?-32768:v;
#endif
}
static int sar(int v,unsigned n){/* Both callers bound v to[-65535,65535], n=1or2. Offset keeps
 * the shift nonnegative and exactly equals floor division for negatives. */
 return ((v+65536)>>n)-(65536>>n);}
#ifdef IMPORT469
__attribute__((section(".conv469.text.pack513"),noinline))
#endif
void wb_pack(wb_encoder*s,const int16_t pcm[560],uint8_t out[512]){
 int16_t stored[280];int peak=0;
 for(unsigned i=0;i<140;i++)for(unsigned c=0;c<2;c++){int v=sar((int)pcm[i*4+c]+pcm[i*4+2+c]+1,1);stored[i*2+c]=v;int a=v<0?-v:v;if(a>peak)peak=a;}
 unsigned sh=0;while(sh<3&&(peak*(1<<(sh+1)))<32000)sh++;
 if(sh!=s->shift){s->error[0]=s->error[1]=0;s->shift=sh;}
 memset(out,0,512);memcpy(out,"P14S",4);out[5]=1;out[11]=0x5b;out[12]=24;out[13]=1;out[14]=sh;
 for(unsigned i=0;i<70;i++){
  uint32_t q[4];for(unsigned k=0;k<4;k++){unsigned c=k&1;int v=clip591((int)stored[i*4+k]*(1<<sh)-s->error[c]);
   int n=sar(v+2,2);if(n>8191)n=8191;/* v is signed16: n>=-8192 and error is in[-3,2]. */int e=n*4-v;s->error[c]=e;q[k]=(uint32_t)n&16383;
  }uint32_t a=q[0]<<18|q[1]<<4|q[2]>>10,b=q[2]<<14|q[3];uint8_t*p=out+16+i*7;
  p[0]=a>>24;p[1]=a>>16;p[2]=a>>8;p[3]=a;p[4]=b>>16;p[5]=b>>8;p[6]=b;
 }
}
