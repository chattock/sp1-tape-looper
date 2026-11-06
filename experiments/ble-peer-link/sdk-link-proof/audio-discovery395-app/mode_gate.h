#include "release_policy.h"
#ifndef SP1_MODE_GATE_H
#define SP1_MODE_GATE_H
#include <stdint.h>
#include <stddef.h>
/* Fixed once per boot. Exact repeats acknowledge without profile restart. */
struct sp1_mode_gate { uint32_t nonce; uint8_t ready,mode,failed; };
static uint16_t sp1_u16(const uint8_t*p){return p[0]|((uint16_t)p[1]<<8);}
static uint32_t sp1_u32(const uint8_t*p){return sp1_u16(p)|((uint32_t)sp1_u16(p+2)<<16);}
static int sp1_select(struct sp1_mode_gate*g,const uint8_t*p,size_t n){
 if(!p||n!=12||p[0]!='S'||p[1]!='P'||p[2]!='U'||p[3]!='1'||p[4]!=1||p[5]<1||p[5]>5||p[6]||p[7]||!sp1_u32(p+8))return -1;
 if(!g->ready||g->failed)return -2;
 if(g->mode)return g->mode==p[5]&&g->nonce==sp1_u32(p+8)?0:-3;
 g->mode=p[5];g->nonce=sp1_u32(p+8);return 1;
}
static int sp1_channel(const struct sp1_mode_gate*g,unsigned op,size_t n){
 if(!g->ready||g->failed)return 0;
 return sr_channel(g->mode,op,n);
}
#endif
