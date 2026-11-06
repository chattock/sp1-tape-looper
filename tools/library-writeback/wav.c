#include "wav.h"
#include <string.h>
__attribute__((weak)) void *wav_native_work509(size_t*bytes){return wav_work469(bytes);}
#include <limits.h>
#include <math.h>
#define DR_MP3_ONLY_MP3
#define DR_MP3_FLOAT_OUTPUT
#define DR_MP3_NO_STDIO
#define DR_MP3_NO_SIMD
#define DR_MP3_IMPLEMENTATION
#include "../import469/vendor/dr_mp3.h"
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#define DR_FLAC_NO_SIMD
#define DR_FLAC_IMPLEMENTATION
#include "../import469/vendor/dr_flac.h"
#include "../import469/filter.h"
#define MAX_FRAMES469 (35840u*643u)
static uint32_t u32(const uint8_t*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static unsigned u16(const uint8_t*p){return p[0]|(unsigned)p[1]<<8;}
static uint32_t be32(const uint8_t*);
static uint32_t word(const uint8_t*p,unsigned n,bool be){uint32_t v=0;for(unsigned j=0;j<n;j++)v|=(uint32_t)p[be?n-j-1:j]<<(8*j);return v;}
static bool read_at(wav_source s,uint32_t at,void *out,uint32_t n){return s.read&&at<=s.bytes&&n<=s.bytes-at&&s.read(s.ctx,at,out,n);}
static bool rate469(unsigned r){switch(r){case 8000:case 11025:case 12000:case 16000:case 22050:case 24000:case 32000:case 44100:case 48000:case 88200:case 96000:return true;default:return false;}}
static wav_result finish469(wav_info *v){
 if(!rate469(v->rate)||v->channels<1||v->channels>2)return WAV_FORMAT;
 if(!v->source_frames)return WAV_EMPTY;
 uint64_t frames=((uint64_t)v->source_frames*48000+v->rate/2)/v->rate;
 if(!frames||frames>UINT32_MAX-279u)return WAV_COMPLEX;v->frames=(uint32_t)frames;return WAV_OK;
}
typedef struct {
 wav_source source;wav_info info;uint32_t magic,position,next,decoded,loaded;
 bool io,failed,mp3_open;drmp3 mp3;drflac *flac;
 uint8_t *arena;size_t used,capacity;
 float pcm[512*2],ring[128][2];uint32_t pcm_at,pcm_n;
 uint8_t cache[2][1024];uint32_t cache_at[2],cache_n[2],cache_turn;
} convert469;
/*510: uncompressed sampling/resampling uses only convert469 fields, never
 * the compressed-decoder allocation arena. Borrow its unused tail for16
 * encoded output sectors. MP3/FLAC keep their entire arena and prior path. */
void *wav_output_work510(const wav_info*v){
 if(!v||(v->codec!=0&&v->codec!=3))return NULL;size_t bytes=0;void*p=wav_work469(&bytes);uintptr_t a=(uintptr_t)p;
 if(!p||a>UINTPTR_MAX-7||bytes>UINTPTR_MAX-a)return NULL;uintptr_t start=(a+7u)&~(uintptr_t)7u,end=(a+bytes)&~(uintptr_t)7u;
 if(end<start||end-start<sizeof(convert469)+16384)return NULL;return (void*)(end-8192);
}
static convert469 *work469(void){size_t n=0;void *p=wav_work469(&n);uintptr_t a=(uintptr_t)p,b=(a+7u)&~(uintptr_t)7u;if(!p||b-a>n||n-(b-a)<sizeof(convert469)+16384)return NULL;n-=b-a;convert469*w=(void*)b;memset(w,0,sizeof *w);w->magic=0x469c0dec;w->arena=(uint8_t*)w+sizeof *w;w->capacity=n-sizeof *w;return w;}
static void *alloc469(size_t n,void*c){convert469*w=c;if(n>SIZE_MAX-7)return NULL;n=(n+7)&~(size_t)7;if(n>w->capacity-w->used)return NULL;void*p=w->arena+w->used;w->used+=n;return p;}
static void free469(void*p,void*c){(void)p;(void)c;}
static void *realloc469(void*p,size_t n,void*c){/* Decoders receive fixed-bounded storage; reject unsupported growth. */return p?NULL:alloc469(n,c);}
static size_t read469(void*c,void*out,size_t n){convert469*w=c;if(n>w->source.bytes-w->position)n=w->source.bytes-w->position;if(!read_at(w->source,w->position,out,n)){w->io=true;return 0;}w->position+=n;return n;}
static bool seek469(convert469*w,int offset,unsigned origin){int64_t at=(origin==0?0:origin==1?w->position:w->source.bytes)+(int64_t)offset;if(at<0||(uint64_t)at>w->source.bytes)return false;w->position=at;return true;}
static drmp3_bool32 seek_mp3(void*c,int o,drmp3_seek_origin z){return seek469(c,o,(unsigned)z);}
static drflac_bool32 seek_flac(void*c,int o,drflac_seek_origin z){return seek469(c,o,(unsigned)z);}
static drmp3_bool32 tell_mp3(void*c,drmp3_int64*p){*p=((convert469*)c)->position;return 1;}
static drflac_bool32 tell_flac(void*c,drflac_int64*p){*p=((convert469*)c)->position;return 1;}
static bool open469(convert469*w,wav_source s,unsigned codec){
 w->source=s;w->info.codec=codec;
 if(codec==1){drmp3_allocation_callbacks a={w,alloc469,realloc469,free469};if(!drmp3_init(&w->mp3,read469,seek_mp3,tell_mp3,NULL,w,&a))return false;w->mp3_open=true;w->info.rate=w->mp3.sampleRate;w->info.channels=w->mp3.channels;}
 else {drflac_allocation_callbacks a={w,alloc469,realloc469,free469};w->flac=drflac_open(read469,seek_flac,tell_flac,w,&a);if(!w->flac)return false;w->info.rate=w->flac->sampleRate;w->info.channels=w->flac->channels;}
 return rate469(w->info.rate)&&w->info.channels>=1&&w->info.channels<=2;
}
static unsigned decode469(convert469*w){
 unsigned n=w->info.codec==1?drmp3_read_pcm_frames_f32(&w->mp3,512,w->pcm):drflac_read_pcm_frames_f32(w->flac,512,w->pcm);
 if(w->info.codec==1&&(w->mp3.sampleRate!=w->info.rate||w->mp3.channels!=w->info.channels)){w->failed=true;return 0;}
 for(unsigned i=0;i<n*w->info.channels;i++)if(!isfinite(w->pcm[i])){w->failed=true;return 0;}
 w->pcm_at=0;w->pcm_n=n;return n;
}
static void close469(convert469*w){if(w->mp3_open)drmp3_uninit(&w->mp3);if(w->flac)drflac_close(w->flac);}
static wav_result compressed469(wav_source s,wav_info*out,unsigned codec){
 convert469*w=work469();if(!w)return WAV_COMPLEX;if(!open469(w,s,codec)){wav_result r=w->io?WAV_IO:WAV_FORMAT;close469(w);return r;}
 uint64_t n=0,limit=(uint64_t)MAX_FRAMES469*w->info.rate/48000;
 wav_progress469(1,0,0);unsigned got;while((got=decode469(w))!=0){n+=got;wav_progress469(1,(uint32_t)n,0);if(n>limit){wav_progress469(0,(uint32_t)n,0);close469(w);return WAV_COMPLEX;}}
 wav_result r=w->io?WAV_IO:w->failed?WAV_FORMAT:WAV_OK;
 if(codec==2&&w->flac->totalPCMFrameCount&&n!=w->flac->totalPCMFrameCount)r=WAV_FORMAT;
 if(n>UINT32_MAX)r=WAV_COMPLEX;
 if(codec==1&&w->mp3.totalPCMFrameCount!=DRMP3_UINT64_MAX&&n!=drmp3_get_pcm_frame_count(&w->mp3))r=WAV_FORMAT;
 wav_progress469(0,(uint32_t)n,(uint32_t)n);wav_info v=w->info;v.source_frames=n;v.data_bytes=s.bytes;close469(w);if(r==WAV_OK)r=finish469(&v);if(r==WAV_OK)*out=v;return r;
}
static wav_result riff469(wav_source s,wav_info*out){
 uint8_t b[40];if(s.bytes<12||!read_at(s,0,b,12))return WAV_IO;if(u32(b+4)!=s.bytes-8)return WAV_LAYOUT;
 wav_info v={0};bool fmt=false,data=false;uint32_t at=12;unsigned chunks=0;
 while(at<s.bytes){if(++chunks>1024)return WAV_COMPLEX;if(s.bytes-at<8)return WAV_LAYOUT;if(!read_at(s,at,b,8))return WAV_IO;uint32_t n=u32(b+4);at+=8;if(n>s.bytes-at)return WAV_LAYOUT;
  if(!memcmp(b,"fmt ",4)){
   if(fmt)return WAV_LAYOUT;fmt=true;if(n!=16&&n!=18&&n!=40)return WAV_FORMAT;if(!read_at(s,at,b,n))return WAV_IO;
   unsigned f=u16(b),ch=u16(b+2),bits=u16(b+14);
   if(f==0xfffe){static const uint8_t tail[12]={0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};unsigned mask=u32(b+20);if(n!=40||u16(b+16)!=22||!u16(b+18)||u16(b+18)>bits||(u32(b+24)==3&&u16(b+18)!=bits)||memcmp(b+28,tail,12)||!((ch==1&&(mask==0||mask==4))||(ch==2&&(mask==0||mask==3))))return WAV_FORMAT;f=u32(b+24);}
   else if(n==40||(n==18&&u16(b+16)))return WAV_FORMAT;
   unsigned align=ch*(bits/8),rate=u32(b+4);
   if(!((f==1&&(bits==8||bits==16||bits==24||bits==32))||(f==3&&(bits==32||bits==64)))||(ch!=1&&ch!=2)||!rate469(rate)||u16(b+12)!=align||u32(b+8)!=(uint64_t)rate*align)return WAV_FORMAT;
   v.format=f;v.channels=ch;v.bits=bits;v.rate=rate;
  }else if(!memcmp(b,"data",4)){if(data)return WAV_LAYOUT;data=true;v.data_offset=at;v.data_bytes=n;}
  at+=n;if(n&1){if(at==s.bytes)return WAV_LAYOUT;at++;}
 }
 if(!fmt||!data||!v.channels)return WAV_LAYOUT;unsigned stride=v.channels*(v.bits/8);if(v.data_bytes%stride)return WAV_LAYOUT;v.source_frames=v.data_bytes/stride;wav_result r=finish469(&v);if(r==WAV_OK)*out=v;return r;
}
static wav_result aiff469(wav_source s,wav_info*out){
 uint8_t b[32];if(s.bytes<12||!read_at(s,0,b,12))return WAV_IO;if(be32(b+4)!=s.bytes-8)return WAV_LAYOUT;bool aifc=!memcmp(b+8,"AIFC",4),comm=false,sound=false;wav_info v={.format=1,.big_endian=1};uint32_t at=12;unsigned chunks=0;
 while(at<s.bytes){if(++chunks>1024)return WAV_COMPLEX;if(s.bytes-at<8)return WAV_LAYOUT;if(!read_at(s,at,b,8))return WAV_IO;uint32_t n=be32(b+4);at+=8;if(n>s.bytes-at)return WAV_LAYOUT;
  if(!memcmp(b,"COMM",4)){if(comm||n<(aifc?22u:18u))return WAV_LAYOUT;comm=true;if(!read_at(s,at,b,aifc?22:18))return WAV_IO;
   v.channels=word(b,2,true);v.source_frames=be32(b+2);v.bits=word(b+6,2,true);unsigned exp=word(b+8,2,true);uint64_t mant=(uint64_t)be32(b+10)<<32|be32(b+14);if(exp&0x8000||exp<16383||exp>16400||!(mant>>63))return WAV_FORMAT;
   unsigned shift=63-(exp-16383);uint64_t rate=mant>>shift;if((mant&(((uint64_t)1<<shift)-1))||rate>UINT32_MAX)return WAV_FORMAT;v.rate=rate;
   if(aifc){if(!memcmp(b+18,"sowt",4))v.big_endian=0;else if(memcmp(b+18,"NONE",4))return WAV_FORMAT;}
   if((v.channels!=1&&v.channels!=2)||!(v.bits==8||v.bits==16||v.bits==24||v.bits==32))return WAV_FORMAT;
  }else if(!memcmp(b,"SSND",4)){if(sound||n<8)return WAV_LAYOUT;sound=true;if(!read_at(s,at,b,8))return WAV_IO;uint32_t off=be32(b);if(be32(b+4)||off>n-8)return WAV_LAYOUT;v.data_offset=at+8+off;v.data_bytes=n-8-off;}
  at+=n;if(n&1){if(at==s.bytes)return WAV_LAYOUT;at++;}
 }
 if(!comm||!sound)return WAV_LAYOUT;if((uint64_t)v.source_frames*v.channels*(v.bits/8)!=v.data_bytes)return WAV_LAYOUT;v.codec=3;wav_result r=finish469(&v);if(r==WAV_OK)*out=v;return r;
}
wav_result wav_probe469(wav_source s,wav_info*out){
 if(!out)return WAV_LAYOUT;memset(out,0,sizeof *out);uint8_t b[12];if(s.bytes<12)return WAV_LAYOUT;if(!read_at(s,0,b,12))return WAV_IO;
 if(!memcmp(b,"RIFF",4)&&!memcmp(b+8,"WAVE",4))return riff469(s,out);
 if(!memcmp(b,"FORM",4)&&(!memcmp(b+8,"AIFF",4)||!memcmp(b+8,"AIFC",4)))return aiff469(s,out);
 if(!memcmp(b,"fLaC",4))return compressed469(s,out,2);
 if(!memcmp(b,"ID3",3)||(b[0]==0xff&&(b[1]&0xe0)==0xe0))return compressed469(s,out,1);
 return WAV_FORMAT;
}
static bool sample469(convert469*w,uint32_t frame,unsigned ch,float*out){
 /*606: PCM sampling does not use the compressed decoder's4096-byte PCM
  * block. Reuse it as encoded read-ahead, without enlarging workspace or
  * touching the history ring/output arena. Cache pages contain whole frames. */
 const wav_info*v=&w->info;unsigned stride=v->channels*(v->bits/8),at=frame*stride+(ch<v->channels?ch:0)*(v->bits/8),page_bytes=(sizeof w->pcm/stride)*stride,page=(frame/(sizeof w->pcm/stride))*page_bytes;
 uint8_t*cache=(uint8_t*)w->pcm;
 if(!w->cache_n[0]||w->cache_at[0]!=page){unsigned n=v->data_bytes-page;if(n>page_bytes)n=page_bytes;if(!read_at(w->source,v->data_offset+page,cache,n)){w->io=true;return false;}w->cache_at[0]=page;w->cache_n[0]=n;}
 const uint8_t*p=cache+(at-page);unsigned bytes=v->bits/8;
 if(v->format==3){if(bytes==4){uint32_t x=u32(p);memcpy(out,&x,4);}else{uint64_t x=u32(p)|((uint64_t)u32(p+4)<<32);double d;memcpy(&d,&x,8);*out=d>1?1:d< -1?-1:(float)d;}if(!isfinite(*out))return false;if(*out>1)*out=1;else if(*out< -1)*out=-1;return true;}
 if(bytes==1){*out=v->codec==3?(int8_t)*p/128.0f:((int)*p-128)/128.0f;return true;}
 uint32_t x=word(p,bytes,v->big_endian);int32_t z=bytes==4?(int32_t)x:(int32_t)((x^(1u<<(bytes*8-1)))-(1u<<(bytes*8-1)));*out=(float)z/(bytes==2?32768.0f:bytes==3?8388608.0f:2147483648.0f);return true;
}
static bool source469(convert469*w,uint32_t frame,unsigned ch,float*out){
 if(w->info.codec!=1&&w->info.codec!=2){
  /*604: PCM/AIFF use the decoder's already-reserved history ring too. Decode
   * a source frame once, rather than once per tap/channel/output frame. The
   *64-tap lookahead fits in128 stereo slots. pcm_n marks this PCM window and
   *pcm_at is its first valid frame; compressed decoder fields are unchanged.
   * A fresh/random seek primes at most64 frames, never the whole source. */
  if(!w->pcm_n||frame<w->pcm_at||(frame>=w->loaded&&frame-w->loaded>=128)){
   w->loaded=frame>63?frame-63:0;w->pcm_at=w->loaded;w->pcm_n=1;
  }
  while(w->loaded<=frame){unsigned at=w->loaded&127u;
   if(!sample469(w,w->loaded,0,&w->ring[at][0]))return false;
   if(w->info.channels==1)w->ring[at][1]=w->ring[at][0];else if(!sample469(w,w->loaded,1,&w->ring[at][1]))return false;
   w->loaded++;if(w->loaded-w->pcm_at>128)w->pcm_at=w->loaded-128;
  }
  *out=w->ring[frame&127u][ch];return true;
 }
 while(w->loaded<=frame){if(w->pcm_at>=w->pcm_n&&!decode469(w))return false;unsigned at=w->loaded++&127u;for(unsigned j=0;j<2;j++)w->ring[at][j]=w->pcm[w->pcm_at*w->info.channels+(j<w->info.channels?j:0)];w->pcm_at++;}
 if(w->loaded-frame>128)return false;*out=w->ring[frame&127u][ch];return true;
}
static int16_t quant469(float x){if(x>=1)return 32767;if(x<=-1)return -32768;return (int16_t)(int32_t)(x*32768.0f);}
bool wav_frames(wav_source s,const wav_info*v,uint32_t first,uint32_t count,int16_t*out,uint32_t cap){
 if(!v||!out||count>cap||count>UINT32_MAX/4)return false;memset(out,0,(size_t)count*4);if(first>v->frames||count>v->frames-first||!rate469(v->rate)||v->channels<1||v->channels>2||!v->source_frames||v->frames!=((uint64_t)v->source_frames*48000+v->rate/2)/v->rate)return false;
 /*504: canonical web WAVs need no resampler/float conversion. Read into
  * the existing output buffer, then expand mono backwards in place. Exact
  * signed16 samples, full source bounds, and no converter workspace growth. */
 if(v->rate==48000&&v->format==1&&v->bits==16&&(v->codec==0||v->codec==3)){
  uint32_t stride=v->channels*2u;
  if((uint64_t)v->source_frames*stride!=v->data_bytes||v->data_offset>s.bytes||v->data_bytes>s.bytes-v->data_offset)return false;
  wav_progress469(2,first,v->frames);
  if(count&&!read_at(s,v->data_offset+first*stride,out,count*stride)){
   memset(out,0,(size_t)count*4);wav_progress469(0,first,v->frames);return false;
  }
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  /*508: little-endian stereo PCM16 is already the exact int16 output after
   * read_at. Keep endian conversion/mono expansion for every other case. */
  if(v->big_endian||v->channels!=2){
#endif
  const uint8_t*b=(const uint8_t*)out;
  for(uint32_t i=count;i>0;){--i;unsigned at=i*stride;
   uint32_t l=word(b+at,2,v->big_endian),r=v->channels==2?word(b+at+2,2,v->big_endian):l;
   out[i*2]=(int32_t)(l^32768u)-32768;out[i*2+1]=(int32_t)(r^32768u)-32768;
  }
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  }
#endif
  if(first+count==v->frames)wav_progress469(0,v->frames,v->frames);
  return true;
 }
 /* Workspace survives only a sequential run for this immutable source. */
 size_t bytes;void*raw=wav_work469(&bytes);if(!raw||bytes<sizeof(convert469)+16392)return false;convert469*w=(void*)(((uintptr_t)raw+7u)&~(uintptr_t)7u);
 if(!first||w->magic!=0x469c0dec||w->next!=first||memcmp(&w->info,v,sizeof *v)||w->source.ctx!=s.ctx||w->source.read!=s.read||w->source.bytes!=s.bytes){w=work469();if(!w)return false;if(v->codec==1||v->codec==2){if(!open469(w,s,v->codec))return false;if(w->info.rate!=v->rate||w->info.channels!=v->channels)return false;}w->source=s;w->info=*v;}
 wav_progress469(2,first,v->frames);
 /*605: advance the exact rational source position with32-bit remainders.
  * Only the initial seek needs64-bit division. Same rounded phase/coefficients. */
 uint64_t initial=(uint64_t)first*v->rate;uint32_t source_frame=initial/48000,remainder=initial%48000;
 for(uint32_t i=0;i<count;i++){
  uint32_t center=source_frame,phase=(remainder*64u+24000u)/48000u;if(phase==64){center++;phase=0;}
  unsigned kernel=v->rate<=48000?0:v->rate==88200?1:2;bool interior=false;
  if(v->rate!=48000){
   uint64_t high64=(uint64_t)center+32;uint32_t high=high64>=v->source_frames?v->source_frames-1:(uint32_t)high64;
   uint32_t low=center>31?center-31:0;if(low>=v->source_frames)low=v->source_frames-1;
   float ignored;if(!source469(w,high,0,&ignored))goto fail;
   /* Both PCM and compressed branches have now retained the whole window.
    * Check that contract before direct lookups; invalid state fails closed. */
   if(w->loaded<=high||w->loaded-low>128||((v->codec!=1&&v->codec!=2)&&low<w->pcm_at))goto fail;
   interior=center>=31&&high64<v->source_frames;
  }
  /*606: share coefficient conversion, addressing and loop control across
   * stereo channels. Each accumulator keeps its original64-tap order.
   * A mono decoder duplicates source samples exactly, so filter it once. */
  float left=0,right=0;
  if(v->rate==48000){
   if(!source469(w,first+i,0,&left))goto fail;
   if(v->channels==1)right=left;else if(!source469(w,first+i,1,&right))goto fail;
  }else{
   const int16_t*coeff=filter469[kernel][phase];
   if(v->channels==1){
    if(interior){unsigned slot=(center-31)&127u;
     for(unsigned tap=0;tap<64;tap++)left+=w->ring[(slot+tap)&127u][0]*coeff[tap];
    }else{
     for(unsigned tap=0;tap<64;tap++){int64_t index=(int64_t)center+tap-31;if(index<0)index=0;if((uint64_t)index>=v->source_frames)index=v->source_frames-1;left+=w->ring[(uint32_t)index&127u][0]*coeff[tap];}
    }
    left*=1.0f/32768;right=left;
   }else{
    if(interior){unsigned slot=(center-31)&127u;
     for(unsigned tap=0;tap<64;tap++){unsigned at=(slot+tap)&127u;float c=coeff[tap];left+=w->ring[at][0]*c;right+=w->ring[at][1]*c;}
    }else{
     for(unsigned tap=0;tap<64;tap++){int64_t index=(int64_t)center+tap-31;if(index<0)index=0;if((uint64_t)index>=v->source_frames)index=v->source_frames-1;unsigned at=(uint32_t)index&127u;float c=coeff[tap];left+=w->ring[at][0]*c;right+=w->ring[at][1]*c;}
    }
    left*=1.0f/32768;right*=1.0f/32768;
   }
  }
  if(!isfinite(left)||!isfinite(right))goto fail;
  out[(size_t)i*2]=quant469(left);out[(size_t)i*2+1]=quant469(right);
  remainder+=v->rate;source_frame+=remainder/48000u;remainder%=48000u;
 }
 w->next=first+count;if(w->next==v->frames)wav_progress469(0,w->next,v->frames);return true;
fail:wav_progress469(0,first,v->frames);memset(out,0,(size_t)count*4);w->next=UINT32_MAX;return false;
}
wav_result wav_inspect(wav_source s,wav_info*out){
 wav_result r=wav_probe469(s,out);if(r!=WAV_OK)return r;
 /*606: retain the complete float finite-value scan, using the existing
  * offline decoder's PCM block for4096-byte reads instead of512-byte stack
  * reads. No decoder is active during this uncompressed-source validation. */
 if(out->format==3){convert469*w=work469();if(!w){memset(out,0,sizeof *out);return WAV_COMPLEX;}uint8_t*b=(uint8_t*)w->pcm;
  for(uint32_t at=0;at<out->data_bytes;){unsigned n=out->data_bytes-at;if(n>sizeof w->pcm)n=sizeof w->pcm;if(!read_at(s,out->data_offset+at,b,n)){memset(out,0,sizeof *out);return WAV_IO;}for(unsigned j=0;j<n;j+=out->bits/8)if(out->bits==32?(u32(b+j)&0x7f800000)==0x7f800000:(u32(b+j+4)&0x7ff00000)==0x7ff00000){memset(out,0,sizeof *out);return WAV_FORMAT;}at+=n;}}
 return WAV_OK;
}
#include "../import469/apple.inc"
