#include "audio_decode.h"
#include "oi_codec_sbc.h"
#include <string.h>
#define Q 3200u
#include "workspace.h"
union bt_workspace bt_workspace;
#define queue bt_workspace.audio
static uint8_t frame[128];
static uint32_t wr,rd;
/* Separate physical cursors: capacity need not be a power of two, while
 * unsigned sequence subtraction remains valid across counter rollover. */
static unsigned wpos,rpos,prime_bytes,rise;
static unsigned at(unsigned offset){unsigned p=rpos+offset;return p>=Q?p-Q:p;}
static void consume(unsigned count,uint32_t old){rpos+=count;if(rpos>=Q)rpos-=Q;__atomic_store_n(&rd,old+count,__ATOMIC_RELEASE);}
unsigned audio_decode_buffered(void){return __atomic_load_n(&wr,__ATOMIC_ACQUIRE)-__atomic_load_n(&rd,__ATOMIC_ACQUIRE);}


static OI_CODEC_SBC_DECODER_CONTEXT ctx;
static OI_UINT32 state[CODEC_DATA_WORDS(2,SBC_CODEC_MIN_FILTER_BUFFERS)];
static int16_t pcm[256];
static unsigned pcm_pos,pcm_frames,rate,phase;
unsigned audio_decode_rate(void){return rate;}
static bool primed;
static unsigned usb_weight; /* 0..128, audio consumer only */
static inline int16_t blend(int16_t bt,int16_t usb,unsigned w){
 if(!w)return bt;
 if(w==128)return ((int32_t)bt+usb)/2;
 return ((int32_t)bt*(256-w)+(int32_t)usb*w)/256;
}
static inline void mix_ramp(int mix){unsigned target=mix?128u:0u;if(usb_weight<target)usb_weight+=2;else if(usb_weight>target)usb_weight-=2;}
static int16_t last_l,last_r;
static unsigned fade;
struct audio_decode_stats audio_decode_stats;
struct audio_decode_profile audio_decode_profile;
static inline uint32_t decode_cycles(void){
#if defined(__ARM_ARCH_7EM__)
 return *(volatile uint32_t*)0xE0001004u; /* DWT CYCCNT, already enabled by engine. */
#else
 return 0;
#endif
}
__attribute__((weak)) void audio_decode_gap_event(int open,unsigned samples){(void)open;(void)samples;}
static void decoder_reset(void){OI_CODEC_SBC_DecoderReset(&ctx,state,sizeof state,2,2,0);}
void audio_decode_reset(unsigned prebuffer){wr=rd=0;wpos=rpos=rise=0;prime_bytes=prebuffer<512?512:prebuffer>2048?2048:prebuffer;pcm_pos=pcm_frames=phase=0;rate=0;usb_weight=0;primed=false;last_l=last_r=0;fade=0;memset(&audio_decode_stats,0,sizeof audio_decode_stats);memset(&audio_decode_profile,0,sizeof audio_decode_profile);decoder_reset();}
bool audio_decode_packet(unsigned bytes){
 uint32_t w=__atomic_load_n(&wr,__ATOMIC_RELAXED),r=__atomic_load_n(&rd,__ATOMIC_ACQUIRE);
 if(bytes>Q-(w-r)){if(w-r>audio_decode_stats.reject_peak)audio_decode_stats.reject_peak=w-r;++audio_decode_stats.overflow;return false;}return true;
}
void audio_decode_byte(uint8_t b){
 uint32_t w=__atomic_load_n(&wr,__ATOMIC_RELAXED),r=__atomic_load_n(&rd,__ATOMIC_ACQUIRE);
 if(w-r==Q){++audio_decode_stats.overflow;return;}
 queue[wpos]=b;if(++wpos==Q)wpos=0;__atomic_store_n(&wr,w+1,__ATOMIC_RELEASE);
 if(w-r+1>audio_decode_stats.peak_queue)audio_decode_stats.peak_queue=w-r+1;
}
/* Single UART producer, single audio consumer: publish once after both spans
 * are copied. The consumer can only free space while this chunk is copied. */
void audio_decode_bytes(const uint8_t *data,unsigned bytes){
 uint32_t w=__atomic_load_n(&wr,__ATOMIC_RELAXED),r=__atomic_load_n(&rd,__ATOMIC_ACQUIRE);
 unsigned available=Q-(w-r),n=bytes<available?bytes:available;
 if(n){unsigned first=Q-wpos;if(first>n)first=n;memcpy(queue+wpos,data,first);if(first<n)memcpy(queue,data+first,n-first);
  wpos+=n;if(wpos>=Q)wpos-=Q;__atomic_store_n(&wr,w+n,__ATOMIC_RELEASE);
  if(w-r+n>audio_decode_stats.peak_queue)audio_decode_stats.peak_queue=w-r+n;}
 audio_decode_stats.overflow+=bytes-n;
}
/* The transport may include RTP/AVDTP headers. Scan only the audio opcode,
 * accept the negotiated stereo/16-block/8-subband formats, then require the
 * decoder CRC check. Never scan diagnostics or accept unsupported rates. */
static bool decode_next(void){
 for(unsigned budget=0;budget<Q;budget++){
  uint32_t r=__atomic_load_n(&rd,__ATOMIC_RELAXED),w=__atomic_load_n(&wr,__ATOMIC_ACQUIRE),n=w-r;
  if(n<4)return false;
  unsigned h=queue[at(1)],bp=queue[at(2)],mode=(h>>2)&3;
  unsigned len=4+8+(16*bp+(mode==3?8:0)+7)/8;
  if(queue[rpos]!=0x9c||(h>>6)<2||(h&0x33)!=0x31||mode<2||bp<2||bp>53||len>sizeof frame){
   consume(1,r);++audio_decode_stats.skipped;continue;}
  if(n<len)return false;
  /* Keep the padded scratch copy: the SBC bit reader may look beyond the
   * encoded frame boundary. A direct pointer at the queue end is unsafe. */
  unsigned first=Q-rpos;if(first>len)first=len;memcpy(frame,queue+rpos,first);if(first<len)memcpy(frame+first,queue,len-first);
  unsigned newrate=(h>>6)==3?48000:44100;
  if(rate!=newrate){decoder_reset();rate=newrate;phase=0;}
  const OI_BYTE *p=frame;OI_UINT32 remain=len,out=sizeof pcm;
  uint32_t started=decode_cycles();
  int status=OI_CODEC_SBC_DecodeFrame(&ctx,&p,&remain,pcm,&out);
  uint32_t elapsed=decode_cycles()-started;
  audio_decode_profile.cycles+=elapsed;++audio_decode_profile.calls;
  if(elapsed>audio_decode_profile.max_frame_cycles)audio_decode_profile.max_frame_cycles=elapsed;
  if(!status)++audio_decode_profile.frames;
  if(status||remain||out!=sizeof pcm){decoder_reset();++audio_decode_stats.errors;consume(1,r);continue;}
  consume(len,r);pcm_pos=0;pcm_frames=128;++audio_decode_stats.frames;return true;
 }
 return false;
}
static unsigned render(int16_t *out,unsigned frames,unsigned gain_q8,bool *selected,int mix){
 audio_decode_profile.cycles=0;audio_decode_profile.frames=0;
 unsigned produced=0;if(gain_q8>256)gain_q8=256;
 /* Unity live-input/recording level. Listening master gain is downstream.
  * Keep existing rational48k conversion and bounded0..256 Q8 input gain. */
 for(unsigned i=0;i<frames;i++){
  /* Steady decoded span: no per-sample priming/shortage/fade checks.
   * Same sample scaling and rational phase updates; edge paths stay below. */
  if(primed&&!rise&&!audio_decode_stats.gap_open&&pcm_pos<pcm_frames && (mix<0 || usb_weight==(mix?128u:0u))){
   unsigned j=i;
   /* Bound the span once. The phase accumulator still performs exactly the
    * same sample repeats at44.1k; no end-of-PCM comparison per sample. */
   unsigned span=rate==48000u ? pcm_frames-pcm_pos :
    ((pcm_frames-pcm_pos)*48000u-phase+rate-1u)/rate;
   if(span>frames-i)span=frames-i;
   unsigned end=i+span;
   /* 341: steady 48 kHz unity input needs neither resampling nor gain
    * multiplication. Choose the mix once per span; preserve C /2 rounding. */
   if(mix>=0 && rate==48000 && gain_q8==256){
    const int16_t *src=pcm+2*pcm_pos;
    int16_t *dst=out+2*i;
    if(usb_weight==128){
     for(unsigned k=0;k<2*span;k++)dst[k]=((int32_t)src[k]+dst[k])/2;
    }else memcpy(dst,src,2*span*sizeof(*dst));
    last_l=src[2*span-2];last_r=src[2*span-1];pcm_pos+=span;
    fade=32;produced+=span;i=end-1;continue;
   }
   /*345: unity44.1k uses the same phase/repeat sequence without per-sample
    * gain multiplication or mix selection. This benefits every tape speed,
    * recording and monitoring; takeover/fade/gain transitions stay below. */
   if(mix>=0 && gain_q8==256){
    int16_t tail_l=0,tail_r=0;
    if(usb_weight==128){
     do {
      tail_l=pcm[2*pcm_pos];tail_r=pcm[2*pcm_pos+1];
      out[2*j]=((int32_t)tail_l+out[2*j])/2;
      out[2*j+1]=((int32_t)tail_r+out[2*j+1])/2;
      phase+=rate;if(phase>=48000){phase-=48000;++pcm_pos;}++j;
     }while(j<end);
    }else{
     do {
      tail_l=pcm[2*pcm_pos];tail_r=pcm[2*pcm_pos+1];
      out[2*j]=tail_l;out[2*j+1]=tail_r;
      phase+=rate;if(phase>=48000){phase-=48000;++pcm_pos;}++j;
     }while(j<end);
    }
    last_l=tail_l;last_r=tail_r;fade=32;produced+=j-i;i=j-1;continue;
   }
   if(mix>=0){
    int16_t tail_l=0,tail_r=0;
    do {
     tail_l=(int32_t)pcm[2*pcm_pos]*gain_q8/256;tail_r=(int32_t)pcm[2*pcm_pos+1]*gain_q8/256;
     out[2*j]=blend(tail_l,out[2*j],usb_weight);out[2*j+1]=blend(tail_r,out[2*j+1],usb_weight);
     phase+=rate;if(phase>=48000){phase-=48000;++pcm_pos;}++j;
    }while(j<end);
    last_l=tail_l;last_r=tail_r;fade=32;produced+=j-i;i=j-1;continue;
   }
   if(gain_q8==256){
    /* Unity path: signed16 PCM copy, no attenuation or amplification. */
    do {
     out[2*j]=pcm[2*pcm_pos];
     out[2*j+1]=pcm[2*pcm_pos+1];
     phase+=rate;if(phase>=48000){phase-=48000;++pcm_pos;}
     ++j;
    } while(j<end);
   }else{
   do {
    out[2*j]=(int32_t)pcm[2*pcm_pos]*gain_q8/256;
    out[2*j+1]=(int32_t)pcm[2*pcm_pos+1]*gain_q8/256;
    phase+=rate;if(phase>=48000){phase-=48000;++pcm_pos;}
    ++j;
   } while(j<end);
   }
   last_l=out[2*(j-1)];last_r=out[2*(j-1)+1];fade=32;
   produced+=j-i;i=j-1;continue;
  }
  if(!primed){uint32_t w=__atomic_load_n(&wr,__ATOMIC_ACQUIRE),r=__atomic_load_n(&rd,__ATOMIC_RELAXED);if(w-r>=prime_bytes){primed=true;rise=64;++audio_decode_stats.primings;}}
  if(!primed||(pcm_pos>=pcm_frames&&!decode_next())){
   primed=false;
   if(selected && !*selected)continue;
   int16_t a=(int32_t)last_l*fade/32,b=(int32_t)last_r*fade/32;
   if(mix>=0){mix_ramp(mix);out[2*i]=blend(a,out[2*i],usb_weight);out[2*i+1]=blend(b,out[2*i+1],usb_weight);}
   else{out[2*i]=a;out[2*i+1]=b;}
   if(fade)--fade;
   ++audio_decode_stats.underrun;if(!audio_decode_stats.output&&!produced)++audio_decode_stats.initial_wait;else {if(!audio_decode_stats.gap_open)audio_decode_gap_event(1,0);++audio_decode_stats.rebuffer_wait;++audio_decode_stats.gap_open;}continue;
  }
  if(audio_decode_stats.gap_open){unsigned n=audio_decode_stats.gap_open;audio_decode_gap_event(0,n);++audio_decode_stats.gap_count;audio_decode_stats.gap_samples+=n;if(n>audio_decode_stats.gap_max)audio_decode_stats.gap_max=n;audio_decode_stats.gap_open=0;}
  int16_t usb_l=selected?out[2*i]:0,usb_r=selected?out[2*i+1]:0;
  bool takeover=selected && !*selected;
  out[2*i]=(int32_t)pcm[2*pcm_pos]*gain_q8/256;
  out[2*i+1]=(int32_t)pcm[2*pcm_pos+1]*gain_q8/256;
  unsigned rise_before=rise;
  int16_t raw_bt_l=out[2*i],raw_bt_r=out[2*i+1];
  if(rise){out[2*i]=(int32_t)out[2*i]*(64-rise)/64;out[2*i+1]=(int32_t)out[2*i+1]*(64-rise)/64;--rise;}
  int16_t bt_l=out[2*i],bt_r=out[2*i+1];
  if(mix>=0){
   mix_ramp(mix);
   if(takeover){
    unsigned bw=(64-rise_before)*(256-usb_weight)/64;
    out[2*i]=((int32_t)raw_bt_l*bw+(int32_t)usb_l*(256-bw))/256;
    out[2*i+1]=((int32_t)raw_bt_r*bw+(int32_t)usb_r*(256-bw))/256;
    if(!rise)*selected=true;
   }else{out[2*i]=blend(bt_l,usb_l,usb_weight);out[2*i+1]=blend(bt_r,usb_r,usb_weight);}
  }
  else if(takeover){out[2*i]+=(int32_t)usb_l*(rise+1)/64;out[2*i+1]+=(int32_t)usb_r*(rise+1)/64;if(!rise)*selected=true;}
  last_l=mix>=0?bt_l:out[2*i];last_r=mix>=0?bt_r:out[2*i+1];fade=32;
  phase+=rate;if(phase>=48000){phase-=48000;++pcm_pos;}
  ++produced;
 }
 audio_decode_stats.output+=produced;return produced;
}

unsigned audio_decode_render(int16_t *out,unsigned frames,unsigned gain){return render(out,frames,gain,0,-1);}
unsigned audio_decode_handoff(int16_t *out,unsigned frames,unsigned gain,bool *selected){return render(out,frames,gain,selected,-1);}
void audio_decode_discard(void){unsigned r=__atomic_load_n(&rd,__ATOMIC_RELAXED),w=__atomic_load_n(&wr,__ATOMIC_ACQUIRE);consume(w-r,r);pcm_pos=pcm_frames=0;primed=false;rise=0;}

unsigned audio_decode_mix(int16_t *out,unsigned frames,unsigned gain,bool *selected,bool usb){return render(out,frames,gain,selected,usb?1:0);}
