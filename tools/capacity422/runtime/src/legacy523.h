#ifndef LEGACY523_H
#define LEGACY523_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "geometry.h"
#define LEGACY523_FIRST 4096u
#define LEGACY523_STRIDE 86016u
static inline uint32_t legacy523_u32(const uint8_t*p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
/* Explicit maintained 3.1.1 geometry only. No repair, writes or blank inference.
 * Pure bounded metadata validation; audio CRC is not available in this format. */
static inline bool legacy523_valid(const uint8_t*p,size_t bytes,uint32_t capacity){
 if(!p||bytes<3072||capacity<LEGACY523_FIRST+64u*LEGACY523_STRIDE||legacy523_u32(p)!=0x53453341u||legacy523_u32(p+4)>=16||legacy523_u32(p+712)>1)return false;
 unsigned sum=0;for(unsigned i=1552;i<2576;i++)sum=(sum+p[i])&65535u;
 bool extended=legacy523_u32(p+1536)==0x53453358u&&p[1540]==1&&!p[1541]&&(p[1542]|(unsigned)p[1543]<<8)==sum;
 for(unsigned s=0;s<16;s++){
  const uint8_t*row=p+8+s*44;uint32_t speed=legacy523_u32(row),song=legacy523_u32(row+4);
  if(speed>131072u||(p[1008+s]&15u)>2u)return false;
  for(unsigned t=0;t<4;t++){
   unsigned lane=s*4+t,present=row[8+t];if(present>1)return false;if(!present)continue;
   if(!extended||!song)return false;
   const uint8_t*x=p+1552+lane*16;unsigned codec=x[12];if(codec<4||codec>6||(x[13]&~7u))return false;
   uint32_t fpb=codec==6?496u:280u,blocks=legacy523_u32(row+12+t*4),content=legacy523_u32(p+716+lane*4),xc=legacy523_u32(x+8);
   if(!blocks)blocks=(uint32_t)(((uint64_t)song+fpb/2)/fpb);
   if(content>LEGACY523_STRIDE||xc>LEGACY523_STRIDE||song>(uint64_t)LEGACY523_STRIDE*496u)return false;
   uint32_t exact=legacy523_u32(x+4);
   if(!exact||exact>(uint64_t)LEGACY523_STRIDE*fpb)return false;
   music_geometry g;if(!music_geometry_load(fpb,song,blocks,legacy523_u32(row+28+t*4),legacy523_u32(x+4),legacy523_u32(x),true,LEGACY523_STRIDE,&g)||g.frames>(uint64_t)LEGACY523_STRIDE*fpb)return false;
  }
 }
 return true;
}
static inline bool legacy523_write_range(uint32_t b,uint32_t n,uint32_t capacity){
 if(!n||b>=capacity||n>capacity-b)return false;
 return b<6u?n<=6u-b:b>=LEGACY523_FIRST&&b<LEGACY523_FIRST+64u*LEGACY523_STRIDE&&n<=LEGACY523_FIRST+64u*LEGACY523_STRIDE-b&&n<=LEGACY523_STRIDE-(b-LEGACY523_FIRST)%LEGACY523_STRIDE;
}
#endif
