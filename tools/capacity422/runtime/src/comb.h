#ifndef SP1_COMB387_H
#define SP1_COMB387_H
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#define COMB_N 512u
/* Controls survive engine switches; DSP history shares reverb's allocation. */
typedef struct {uint8_t level[4],tune[4],decay[4],damp[4],mix;} comb_controls;
#define COMB_DEFAULT { {0,0,0,0},{64,85,101,128},{160,160,160,160},{160,160,160,160},128 }
typedef struct {
 int16_t line[4][COMB_N],fir[64];
 int32_t lp[4],delay[4],level[4],feedback[4],alpha[4];
 int32_t excitation[64],wet[2][64];
 int32_t hp,prev,mix;
 int32_t delta[4][5],goal[4][5],mix_delta,mix_goal;
 uint32_t w,fw,blocks,max_us,clips;
} comb_state;
_Static_assert(sizeof(comb_state)<=8192,"comb fits reverb allocation");
#include "comb_tables.h"
static inline int32_t comb_sat(int32_t x){return x>32767?32767:x< -32768?-32768:x;}
static inline int32_t comb_slew(int32_t x,int32_t t){int32_t d=t-x;return x+(d>0?(d+63)/64:d<0?(d-63)/64:0);}
static inline unsigned comb_pitch_index(uint8_t q,bool snap){return snap?256u+((unsigned)q*48u+127u)/255u:q;}
static inline void comb_targets(const comb_controls *c,bool snap,int32_t delay[4],int32_t fb[4],int32_t alpha[4]){
 for(unsigned v=0;v<4;v++){unsigned d=c->damp[v]>>4;delay[v]=comb_delays[d][comb_pitch_index(c->tune[v],snap)];fb[v]=24576+((int32_t)c->decay[v]*8028)/255;alpha[v]=comb_alpha[d];}
}
static inline void comb_begin(comb_state *s,const comb_controls *c,bool snap){
 int32_t td[4],tf[4],ta[4];comb_targets(c,snap,td,tf,ta);
 for(unsigned v=0;v<4;v++){
  if(!s->delay[v])s->delay[v]=td[v];
  int32_t now[5]={s->delay[v],s->level[v],s->feedback[v],s->alpha[v],0};
  int32_t to[5]={s->delay[v]+(td[v]-s->delay[v])/4,(int32_t)c->level[v]*128,s->feedback[v]+(tf[v]-s->feedback[v])/4,s->alpha[v]+(ta[v]-s->alpha[v])/4,0};
  for(unsigned j=0;j<4;j++){s->goal[v][j]=to[j];s->delta[v][j]=(to[j]-now[j])/64;}
 }
 s->mix_goal=(int32_t)c->mix*128;s->mix_delta=(s->mix_goal-s->mix)/64;
}
static inline void comb_end(comb_state *s){
 for(unsigned v=0;v<4;v++){s->delay[v]=s->goal[v][0];s->level[v]=s->goal[v][1];s->feedback[v]=s->goal[v][2];s->alpha[v]=s->goal[v][3];}s->mix=s->mix_goal;
}
/* Mirrored FIR history makes each32-tap dot product contiguous. ARM uses
 * signed dual16-bit MAC; portable branch exercises the same integer result. */
static inline int32_t comb_input(comb_state *s,const int32_t *l,const int32_t *r){
 for(unsigned j=0;j<4;j++){int16_t x=comb_sat((l[j]/2)+(r[j]/2));s->fir[s->fw]=x;s->fir[s->fw+32]=x;s->fw=(s->fw+1)&31u;}
 int32_t a=0;
 for(unsigned j=0;j<32;j+=2){
#if defined(__arm__) && defined(__ARM_FEATURE_DSP)
  uint32_t x,h;memcpy(&x,s->fir+s->fw+j,4);memcpy(&h,comb_fir+j,4);
  __asm__("smlad %0, %1, %2, %0" : "+r"(a) : "r"(x), "r"(h));
#else
  a+=(int32_t)s->fir[s->fw+j]*comb_fir[j]+(int32_t)s->fir[s->fw+j+1]*comb_fir[j+1];
#endif
 }
 int32_t x=a>>15;s->hp=comb_sat(x-s->prev+((s->hp*32604)>>15));s->prev=x;return s->hp>>3;
}
static inline void comb_step(comb_state *s,int32_t in,int32_t *left,int32_t *right){
 static const uint16_t pan[4]={224,160,96,32};int32_t l=0,r=0;
 for(unsigned v=0;v<4;v++){
  s->delay[v]+=s->delta[v][0];s->level[v]+=s->delta[v][1];s->feedback[v]+=s->delta[v][2];s->alpha[v]+=s->delta[v][3];
  uint32_t d=(uint32_t)s->delay[v],n=d>>12,f=d&4095u;
  int32_t a=s->line[v][(s->w-n)&511u],b=s->line[v][(s->w-n-1)&511u];
  int32_t y=a+(((b-a)*(int32_t)f)>>12);
  s->lp[v]+=(((y-s->lp[v])*s->alpha[v])>>15);
  int32_t z=in+((s->lp[v]*s->feedback[v])>>15);
  if(z>32767||z< -32768)s->clips++;
  s->line[v][s->w]=comb_sat(z);
  int32_t wet=((y*s->level[v])>>15);
  l+=((wet*pan[v])>>8);r+=((wet*(256-pan[v]))>>8);
 }
 s->w=(s->w+1)&511u;s->mix+=s->mix_delta;
 *left=((l*s->mix)>>16);*right=((r*s->mix)>>16);
}

/* Effect-major bank: input filtering once, each voice in a compact loop,
 * output interpolation once. Scratch lives inside the reverb union. */
static __attribute__((optimize("O3"),noinline)) void comb_render(comb_state *s,int32_t *l,int32_t *r,int32_t *prevL,int32_t *prevR){
 for(unsigned k=0;k<64;k++){s->excitation[k]=comb_input(s,l+4*k,r+4*k);s->wet[0][k]=s->wet[1][k]=0;}
 static const int32_t pans[4]={224,160,96,32};
 for(unsigned v=0;v<4;v++){
  int32_t delay=s->delay[v],level=s->level[v],fb=s->feedback[v],alpha=s->alpha[v],lp=s->lp[v];
  for(unsigned k=0;k<64;k++){
   delay+=s->delta[v][0];level+=s->delta[v][1];fb+=s->delta[v][2];alpha+=s->delta[v][3];
   unsigned w=(s->w+k)&511u,n=(unsigned)delay>>12,f=(unsigned)delay&4095u;
   int32_t a=s->line[v][(w-n)&511u],b=s->line[v][(w-n-1)&511u];
   int32_t y=a+(((b-a)*(int32_t)f)>>12);lp+=((y-lp)*alpha)>>15;
   int32_t z=s->excitation[k]+((lp*fb)>>15);if(z>32767||z< -32768)s->clips++;
   s->line[v][w]=comb_sat(z);int32_t wet=(y*level)>>15;
   s->wet[0][k]+=(wet*pans[v])>>8;s->wet[1][k]+=(wet*(256-pans[v]))>>8;
  }s->lp[v]=lp;
 }
 int32_t pl=*prevL,pr=*prevR,mix=s->mix;
 for(unsigned k=0;k<64;k++){
  mix+=s->mix_delta;int32_t ol=(s->wet[0][k]*mix)>>16,orr=(s->wet[1][k]*mix)>>16;
  for(unsigned j=0;j<4;j++){l[k*4+j]+=pl+(ol-pl)*(int32_t)j/4;r[k*4+j]+=pr+(orr-pr)*(int32_t)j/4;}
  pl=ol;pr=orr;
 }s->w=(s->w+64u)&511u;*prevL=pl;*prevR=pr;comb_end(s);
}
#endif
