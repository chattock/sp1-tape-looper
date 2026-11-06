#ifndef SP1_TAKE_SILENCE401_H
#define SP1_TAKE_SILENCE401_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
/* A silent suffix may extend the loop, never replace missing captured data.
 * content==0 means all loop data is content in legacy geometry. */
static inline bool take_silence401_extent(uint32_t have,uint32_t content,uint32_t loop,uint32_t frames,uint32_t fpb,uint32_t capacity,uint32_t *need){
 if(!fpb||!need||have>capacity||content>have)return false;
 uint32_t n=frames/fpb+(frames%fpb!=0u);if(n<loop)n=loop;
 if(n>capacity||(!content&&have<n))return false;
 *need=n;return true;
}
/* Canonical zero PCM blocks in the actual supported native codecs. The
 * P14S payload's signed14-bit samples and P16M signed16-bit samples are zero;
 * valid signatures/counts distinguish them from an invalid all-zero sector.
 * Pure output construction; do not disturb the live encoder shaper/rings. */
static inline void take_silence401(bool mono,uint8_t *out,uint32_t blocks){
 for(uint32_t n=0;n<blocks;n++){
  uint8_t *b=out+n*512u;memset(b,0,512);
  memcpy(b,mono?"P16M":"P14S",4);b[5]=mono?0u:1u;b[11]=0x5bu;
  uint32_t frames=mono?496u:280u;b[12]=(uint8_t)frames;b[13]=(uint8_t)(frames>>8);
  b[14]=mono?0u:3u; /* P14S zero input's canonical block-gain shift. */
 }
}
#endif
