#include "arena.h"
#include <string.h>
#define PREFIX 64u
#define SPOOL 16384u
#define BITMAP 16384u
static uint32_t get(const uint8_t*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=n>>(8*i);}
static void put64(uint8_t*p,uint64_t n){put(p,n);put(p+4,n>>32);}
static uint64_t get64(const uint8_t*p){return get(p)|((uint64_t)get(p+4)<<32);}
static bool overlap(uint32_t a,uint32_t n,uint32_t b,uint32_t m){return (uint64_t)a+n>b&&(uint64_t)b+m>a;}
static bool init(arena422*s,pool_io raw,uint32_t first,uint32_t blocks,uint64_t identity,uint64_t gen,arena422_locked lock,void*gate){
 if(!s||!raw.read||!raw.write||!raw.sync||!lock||!identity||!gen||!first||blocks<131072||blocks>524288||blocks%64||first>=raw.device_blocks||blocks>raw.device_blocks-first)return false;
 if(overlap(first,blocks,raw.pool_first,raw.pool_blocks)||overlap(first,blocks,raw.catalog[0],8192)||overlap(first,blocks,raw.catalog[1],8192))return false;
 memset(s,0,sizeof *s);s->raw=raw;s->first=first;s->blocks=blocks;s->identity=identity;s->generation=gen;s->locked=lock;s->gate=gate;
 for(unsigned i=0;i<2;i++)s->slices[i]=(arena422_slice){s,PREFIX+i*SPOOL,SPOOL};s->slices[2]=(arena422_slice){s,PREFIX+2*SPOOL+BITMAP,SPOOL-64};s->slices[3]=(arena422_slice){s,PREFIX+3*SPOOL+BITMAP-64,64};return true;
}
static bool locked(arena422*s){return s->locked&&s->locked(s->gate,s->identity,s->first);}
static bool valid(arena422*s,const uint8_t*b){return !memcmp(b,"SP1VIEW6",8)&&get(b+508)==~pool_crc(UINT32_MAX,b,508)&&get64(b+8)&&get64(b+64)&&get64(b+64)<=get64(b+8)&&get64(b+16)==s->identity&&get64(b+24)==s->generation&&get(b+32)>=1&&get(b+32)<=5&&get(b+36)==s->first&&get(b+40)==s->blocks&&get(b+44)==s->raw.device_blocks&&get(b+48)==s->raw.pool_first&&get(b+52)==s->raw.pool_blocks&&get(b+56)==s->raw.catalog[0]&&get(b+60)==s->raw.catalog[1];}
static bool publish(arena422*s,unsigned state){
 if(!locked(s)||s->sequence==UINT64_MAX)return false;memset(s->sector,0,512);memcpy(s->sector,"SP1VIEW6",8);put64(s->sector+8,s->sequence+1);put64(s->sector+16,s->identity);put64(s->sector+24,s->generation);put(s->sector+32,state);put(s->sector+36,s->first);put(s->sector+40,s->blocks);put(s->sector+44,s->raw.device_blocks);put(s->sector+48,s->raw.pool_first);put(s->sector+52,s->raw.pool_blocks);put(s->sector+56,s->raw.catalog[0]);put(s->sector+60,s->raw.catalog[1]);put64(s->sector+64,s->epoch);put(s->sector+508,~pool_crc(UINT32_MAX,s->sector,508));
 unsigned target=s->active^1;uint8_t check[512];
 if(!s->raw.write(s->raw.context,s->first+target,s->sector,1)||!s->raw.sync(s->raw.context)||!s->raw.read(s->raw.context,s->first+target,check,1)||memcmp(check,s->sector,512)){s->ready=false;return false;}
 s->active=target;s->sequence++;s->state=state;s->ready=true;return true;
}
bool arena422_open(arena422*s,pool_io raw,uint32_t first,uint32_t blocks,uint64_t identity,uint64_t gen,arena422_locked lock,void*gate){
 if(!init(s,raw,first,blocks,identity,gen,lock,gate)||!locked(s))return false;uint8_t other[512];
 if(!raw.read(raw.context,first,s->sector,1)||!raw.read(raw.context,first+1,other,1))return false;bool a=valid(s,s->sector),b=valid(s,other);
 if(!a&&!b)return false;if(a&&b&&get64(s->sector+8)==get64(other+8)&&memcmp(s->sector,other,512))return false;
 s->active=!a||(b&&get64(other+8)>get64(s->sector+8));const uint8_t*p=s->active?other:s->sector;s->sequence=get64(p+8);s->epoch=get64(p+64);s->state=get(p+32);s->ready=true;return true;
}
bool arena422_create(arena422*s,pool_io raw,uint32_t first,uint32_t blocks,uint64_t identity,uint64_t gen,arena422_locked lock,void*gate){
 if(!init(s,raw,first,blocks,identity,gen,lock,gate)||!locked(s))return false;
 /* Only the migration's blank headers admit a new session. Reuse of a previous
  * workspace needs explicit completed-session retirement, not a create fallback. */
 uint8_t b[512];for(unsigned k=0;k<2;k++){if(!raw.read(raw.context,first+k,b,1))return false;for(unsigned i=0;i<512;i++)if(b[i])return false;}
 s->active=1;s->epoch=1;return publish(s,ARENA422_PREPARING);
}
bool arena422_renew(arena422*s,uint64_t generation){
 if(!s||!s->ready||!locked(s)||!generation||s->sequence==UINT64_MAX||
    (s->state!=ARENA422_COMMITTED&&s->state!=ARENA422_ABORTED)||
    generation<s->generation)return false;
 /* Both terminal states release host ownership. Normal runtime may have
  * published newer recordings/settings since the last visit. Never rewind
  * the catalog, but allow that newer generation in a NEW session.
  * A new epoch distinguishes overlay headers even when an aborted session did
  * not change the catalog. Old staged bytes cannot reopen as this session. */
 s->generation=generation;s->epoch=s->sequence+1;
 return publish(s,ARENA422_PREPARING);
}
bool arena422_transition(arena422*s,unsigned state){
 if(!s||!s->ready)return false;bool allowed=(s->state==1&&(state==2||state==5))||(s->state==2&&(state==3||state==5))||(s->state==3&&(state==4||state==5));
 return allowed&&publish(s,state);
}
static bool slice_read(void*c,uint32_t at,void*b){arena422_slice*x=c;arena422*s=x->arena;return s->ready&&locked(s)&&at<x->blocks&&s->raw.read(s->raw.context,s->first+x->offset+at,b,1);}
static bool slice_write(void*c,uint32_t at,const void*b){arena422_slice*x=c;arena422*s=x->arena;return s->ready&&((x->offset<PREFIX+2*SPOOL&&s->state==ARENA422_PREPARING)||(x->offset>=PREFIX+2*SPOOL+BITMAP&&s->state==ARENA422_COMMITTING))&&locked(s)&&at<x->blocks&&s->raw.write(s->raw.context,s->first+x->offset+at,b,1);}
static bool slice_sync(void*c){arena422*s=((arena422_slice*)c)->arena;return s->ready&&locked(s)&&s->raw.sync(s->raw.context);}
pg422_io arena422_manifest_io(arena422*s,unsigned which){if(!s||!s->ready||which>=4)return(pg422_io){0};return(pg422_io){&s->slices[which],slice_read,slice_write,slice_sync,s->slices[which].blocks};}
static bool ovread(void*c,uint32_t at,void*b,uint32_t n){arena422*s=c;uint32_t start=s->first+PREFIX+3*SPOOL+BITMAP;return s->ready&&locked(s)&&n&&at>=start&&at-s->first<s->blocks&&n<=s->blocks-(at-s->first)&&s->raw.read(s->raw.context,at,b,n);}
static bool ovwrite(void*c,uint32_t at,const void*b,uint32_t n){arena422*s=c;uint32_t start=s->first+PREFIX+3*SPOOL+BITMAP;return s->ready&&(s->state==1||s->state==2)&&locked(s)&&n&&at>=start&&at-s->first<s->blocks&&n<=s->blocks-(at-s->first)&&s->raw.write(s->raw.context,at,b,n);}
static bool ovsync(void*c){arena422*s=c;return s->ready&&locked(s)&&s->raw.sync(s->raw.context);}
ov_io arena422_overlay_io(arena422*s,uint32_t sectors){ov_io io={0};if(!s||!s->ready||!sectors)return io;io=(ov_io){.ctx=s,.read=ovread,.write=ovwrite,.sync=ovsync,.first=s->first+PREFIX+3*SPOOL+BITMAP,.blocks=s->blocks-PREFIX-3*SPOOL-BITMAP,.device_blocks=s->raw.device_blocks,.buckets=s->blocks>=524288?16384u:s->blocks>=262144?4096u:2048u,.virtual_sectors=sectors};/* Old interrupted sessions retain their exact overlay bucket geometry. */
 if(s->state!=ARENA422_PREPARING){uint8_t h[512];if(!s->raw.read(s->raw.context,io.first,h,1))return(ov_io){0};uint32_t buckets=get(h+16);if(buckets!=1024&&buckets!=2048&&buckets!=4096&&buckets!=16384)return(ov_io){0};io.buckets=buckets;}
 put64(io.session,s->identity);put64(io.session+8,s->generation);put(io.session+16,s->first);put(io.session+20,s->blocks);put64(io.session+24,s->epoch);return io;}

static bool bitmap_read(void*c,uint32_t at,void*b){arena422*s=c;return s->ready&&locked(s)&&at<BITMAP&&s->raw.read(s->raw.context,s->first+PREFIX+2*SPOOL+at,b,1);}
static bool bitmap_write(void*c,uint32_t at,const void*b){arena422*s=c;return s->ready&&s->state==ARENA422_COMMITTING&&locked(s)&&at<BITMAP&&s->raw.write(s->raw.context,s->first+PREFIX+2*SPOOL+at,b,1);}
fat422_bitmap arena422_bitmap_io(arena422*s){if(!s||!s->ready)return(fat422_bitmap){0};return(fat422_bitmap){s,bitmap_read,bitmap_write,ovsync,BITMAP};}

/* Select independently validated headers, including a terminal session from an
 * older generation. Equal sequence with unequal content always fails closed. */
bool arena422_latest(arena422*s,pool_io raw,uint32_t first,uint32_t blocks,uint64_t identity,arena422_locked lock,void*gate){
 uint8_t b[512];uint64_t gen[2]={0},seq[2]={0};
 for(unsigned i=0;i<2;i++){if(!raw.read(raw.context,first+i,b,1))return false;
  if(!memcmp(b,"SP1VIEW6",8)&&get(b+508)==~pool_crc(UINT32_MAX,b,508)){gen[i]=get64(b+24);seq[i]=get64(b+8);}}
 unsigned pick=seq[1]>seq[0];if(!gen[pick])return false;
 return arena422_open(s,raw,first,blocks,identity,gen[pick],lock,gate);
}
