#ifndef NATIVE439_H
#define NATIVE439_H
#include <stdint.h>
/* Decode one stored native stereo frame without any shared scratch. Only the
 * partial-block path uses this; whole blocks retain the pinned fast decoder. */
static inline void native439_pair(const uint8_t *b, unsigned spb, unsigned k, int32_t *l, int32_t *r)
{
 if(spb==496u){*l=*r=(int16_t)((uint16_t)b[16+2*k]|(uint16_t)b[17+2*k]<<8);return;}
 const uint8_t *p=b+16+7*(k/2);uint32_t a,v;
 if(!(k&1)){a=(uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];*l=(a>>18)&16383u;*r=(a>>4)&16383u;}
 else {v=(uint32_t)p[3]<<24|(uint32_t)p[4]<<16|(uint32_t)p[5]<<8|p[6];*l=(v>>14)&16383u;*r=v&16383u;}
 if(*l>=8192)*l-=16384;if(*r>=8192)*r-=16384;
 *l=(*l*4)>>(b[14]&3);*r=(*r*4)>>(b[14]&3);
}
/* Emits only the requested interval, with exactly the existing interpolation,
 * final hold and preceding-frame heal. Foreign headers emit silence. Source
 * bytes may occupy ALL decoder scratch: this function never writes there. */
static void __attribute__((optimize("O2"))) native439_slice(int16_t *ring,uint32_t mask,uint32_t start,
 const uint8_t *b,unsigned spb,unsigned off,unsigned n,int16_t prev[2],unsigned mark)
{
 const int valid=b[11]==mark&&b[2]==(spb==496u?0x36u:0x34u);
 int32_t l=0,r=0,nl=0,nr=0;unsigned loaded=UINT32_MAX;
 for(unsigned j=0;j<n;j++){
  unsigned f=off+j,k=f/2;
  if(valid&&loaded!=k){
   if(loaded!=UINT32_MAX&&loaded+1==k){l=nl;r=nr;}else native439_pair(b,spb,k,&l,&r);
   if(k+1<spb/2)native439_pair(b,spb,k+1,&nl,&nr);else {nl=l;nr=r;}
   loaded=k;
  }
  int16_t a=(f&1)?(l+nl+(spb==496u))>>1:l,c=(f&1)?(r+nr+(spb==496u))>>1:r;
  /* Silence must remain zero even for the mono rounding convention. */
  if(!valid)a=c=0;
  if(!f){uint32_t h=((start+j-1)&mask)*2;ring[h]=((int32_t)prev[0]+a)>>1;ring[h+1]=((int32_t)prev[1]+c)>>1;}
  uint32_t d=((start+j)&mask)*2;ring[d]=a;ring[d+1]=c;prev[0]=a;prev[1]=c;
 }
}
static inline uint32_t rearm439_frames(uint32_t speed_q16,uint32_t base)
{
 if(speed_q16<65536u)speed_q16=65536u;
 if(speed_q16>131072u)speed_q16=131072u;
 return (base*speed_q16+65535u)>>16;
}
#endif
