#ifndef SEAM478_H
#define SEAM478_H
#include <stdint.h>
/* Streaming-only, bounded work: 128 frames (~2.67ms at1x) per side.
 * Power-of-two short-loop cap keeps opposite ramps disjoint, no division.
 * Geometry and decoder continuity are never changed. */
static inline unsigned seam478_shift(uint32_t period){
 unsigned s=7;while(s && (1u<<s)>period/2u)s--;return s;
}
static inline int16_t seam478_scale(int16_t sample,uint32_t gain,unsigned shift){
 int32_t v=(int32_t)sample*(int32_t)gain;
 return (int16_t)(v<0?-((-v)>>shift):(v>>shift));
}
static inline uint32_t seam478_previous(const int16_t *ring,uint32_t mask,uint32_t start){
 uint32_t k=((start-1u)&mask)*2u;
 return (uint16_t)ring[k]|(uint32_t)(uint16_t)ring[k+1u]<<16;
}
static void __attribute__((noinline,optimize("O2"))) seam478_apply(int16_t *ring,uint32_t mask,
 uint32_t start,uint32_t frames,uint32_t phase,uint32_t period,uint32_t previous,int forward)
{
 if(period<2u||phase>=period||frames>period-phase||!frames)return;
 unsigned shift=seam478_shift(period);uint32_t width=1u<<shift;
 /* A subsequent forward decoder may heal the preceding published frame.
  * Keep its already-faded value; never attenuate it twice or resurrect it.
  * Interior boundaries retain the exact existing interpolation heal. */
 if(forward){uint32_t p=phase?phase-1u:period-1u;
  if(p<width||period-1u-p<width){uint32_t k=((start-1u)&mask)*2u;
   ring[k]=(int16_t)(uint16_t)previous;ring[k+1u]=(int16_t)(previous>>16);}}
 uint32_t end=phase+frames;
 uint32_t a=phase,b=end<width?end:width;
 for(uint32_t p=a;p<b;p++){uint32_t k=((start+p-phase)&mask)*2u;
  ring[k]=seam478_scale(ring[k],p,shift);
  ring[k+1u]=seam478_scale(ring[k+1u],p,shift);}
 a=phase>period-width?phase:period-width;b=end;
 for(uint32_t p=a;p<b;p++){uint32_t k=((start+p-phase)&mask)*2u,g=period-1u-p;
  ring[k]=seam478_scale(ring[k],g,shift);
  ring[k+1u]=seam478_scale(ring[k+1u],g,shift);}
}
#endif
