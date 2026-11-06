#include "publication.h"
#include <string.h>
#include <limits.h>
static uint32_t get(const uint8_t*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t g64(const uint8_t*p){return get(p)|((uint64_t)get(p+4)<<32);}
static void put(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=n>>(8*i);}
static void p64(uint8_t*p,uint64_t n){put(p,n);put(p+4,n>>32);}
static uint32_t crc(const void*b,unsigned n){return ~pool_crc(UINT32_MAX,b,n);}
static bool allowed(publication422*s){arena422*a=s?s->arena:0;return a&&a->ready&&a->state==ARENA422_COMMITTING&&a->locked&&a->locked(a->gate,a->identity,a->first);}
static uint32_t root(arena422*a,unsigned n){return a->raw.catalog[n/2]+n%2;}
static bool read_raw(publication422*s,uint32_t at,void*b,uint32_t n){return s->arena->raw.read(s->arena->raw.context,at,b,n);}
static bool sync_raw(publication422*s){return s->arena->raw.sync(s->arena->raw.context);}
static bool saved(publication422*s,unsigned n,void*b){return read_raw(s,s->arena->first+2+n,b,1);}
static bool write_checked(publication422*s,uint32_t at,const void*b){uint8_t check[512];arena422*a=s->arena;return a->raw.write(a->raw.context,at,b,1)&&sync_raw(s)&&read_raw(s,at,check,1)&&!memcmp(b,check,512);}
static bool status(publication422*s,unsigned state){
 if(!allowed(s)||s->sequence==UINT64_MAX)return false;uint8_t*b=s->sector;memset(b,0,512);memcpy(b,"PGPUB422",8);p64(b+8,s->sequence+1);p64(b+16,s->arena->identity);p64(b+24,s->arena->generation);p64(b+32,s->arena->epoch);put(b+40,state);put(b+44,s->target);put(b+48,s->original_crc);put(b+52,s->target_crc);put(b+56,s->arena->first);put(b+60,s->arena->raw.catalog[0]);put(b+64,s->arena->raw.catalog[1]);put(b+508,crc(b,508));
 unsigned next=s->active^1;if(!write_checked(s,s->arena->first+6+next,b)){s->ready=false;return false;}s->active=next;s->sequence++;s->state=state;return true;
}
static bool valid(publication422*s,const uint8_t*b){arena422*a=s->arena;return !memcmp(b,"PGPUB422",8)&&get(b+508)==crc(b,508)&&g64(b+8)&&g64(b+16)==a->identity&&g64(b+24)==a->generation&&g64(b+32)==a->epoch&&get(b+40)<=1&&get(b+56)==a->first&&get(b+60)==a->raw.catalog[0]&&get(b+64)==a->raw.catalog[1];}
static bool target_valid(publication422*s,const uint8_t*b){return !memcmp(b,"PG422IDX",8)&&!get(b+8)&&get(b+508)==crc(b,508)&&s->arena->generation<UINT64_MAX&&g64(b+12)==s->arena->generation+1&&get(b+24)>=2&&get(b+24)<=8192&&(!get(b+20)||(get(b+20)>=2&&get(b+20)<get(b+24)));}
bool publication422_create(publication422*s,arena422*a){
 if(!s||!a)return false;memset(s,0,sizeof *s);s->arena=a;s->active=1;if(!allowed(s))return false;
 /* Existing evidence for this epoch may never be replaced. */
 uint8_t check[512];for(unsigned n=0;n<2;n++){if(!read_raw(s,a->first+6+n,check,1))return false;if(valid(s,check))return false;}
 uint32_t sum=UINT32_MAX;
 for(unsigned n=0;n<4;n++){if(!read_raw(s,root(a,n),s->sector,1))return false;sum=pool_crc(sum,s->sector,512);if(!write_checked(s,a->first+2+n,s->sector))return false;}
 s->original_crc=~sum;s->ready=true;return status(s,0);
}
unsigned publication422_open(publication422*s,arena422*a){
 if(!s||!a)return 0;memset(s,0,sizeof *s);s->arena=a;if(!allowed(s))return 0;uint8_t other[512];
 if(!read_raw(s,a->first+6,s->sector,1)||!read_raw(s,a->first+7,other,1))return 0;bool x=valid(s,s->sector),y=valid(s,other);
 if(!x&&!y)return 0;if(x&&y&&g64(s->sector+8)==g64(other+8)&&memcmp(s->sector,other,512))return 0;s->active=!x||(y&&g64(other+8)>g64(s->sector+8));const uint8_t*b=s->active?other:s->sector;
 s->state=get(b+40);s->target=get(b+44);s->original_crc=get(b+48);s->target_crc=get(b+52);s->sequence=g64(b+8);
 uint32_t sum=UINT32_MAX;bool exact=false;
 for(unsigned n=0;n<4;n++){if(!saved(s,n,s->sector)||!read_raw(s,root(a,n),other,1))return 0;sum=pool_crc(sum,s->sector,512);if(!s->state||root(a,n)!=s->target){if(memcmp(other,s->sector,512))return 0;}}
 if(~sum!=s->original_crc)return 0;
 if(s->state){bool address=false;for(unsigned n=0;n<4;n++)address|=s->target==root(a,n);if(!address||!read_raw(s,a->first+8,s->sector,1)||crc(s->sector,512)!=s->target_crc||!target_valid(s,s->sector)||!read_raw(s,s->target,other,1))return 0;exact=!memcmp(other,s->sector,512);}
 s->ready=true;return exact?2:1;
}
static bool rd(void*c,uint32_t at,void*b,uint32_t n){publication422*s=c;return s->ready&&allowed(s)&&read_raw(s,at,b,n);}
static bool wr(void*c,uint32_t at,const void*b,uint32_t n){
 publication422*s=c;if(!s->ready||!allowed(s)||!n)return false;arena422*a=s->arena;
 for(unsigned k=0;k<2;k++)if(at>=a->raw.catalog[k]&&at-a->raw.catalog[k]<2){
  if(n!=1||s->state||!target_valid(s,b))return false;
  if(!write_checked(s,a->first+8,b)){s->ready=false;return false;}s->target=at;s->target_crc=crc(b,512);
  if(!status(s,1))return false;
  if(!a->raw.write(a->raw.context,at,b,1)){s->ready=false;return false;}return true;
 }
 bool bounded=at>=a->raw.pool_first&&at-a->raw.pool_first<a->raw.pool_blocks&&n<=a->raw.pool_blocks-(at-a->raw.pool_first);
 for(unsigned k=0;k<2;k++)bounded|=at>=a->raw.catalog[k]+2&&at-a->raw.catalog[k]<8192&&n<=8192-(at-a->raw.catalog[k]);
 if(!bounded||s->state)return false;if(!a->raw.write(a->raw.context,at,b,n)){s->ready=false;return false;}return true;
}
static bool sy(void*c){publication422*s=c;if(!s->ready||!allowed(s))return false;if(!sync_raw(s)){s->ready=false;return false;}return true;}
pool_io publication422_io(publication422*s){if(!s||!s->ready)return(pool_io){0};pool_io io=s->arena->raw;io.context=s;io.read=rd;io.write=wr;io.sync=sy;return io;}
bool publication422_replay(publication422*s){
 if(!s||!s->ready||!s->state||!allowed(s))return false;publication422 check;if(!publication422_open(&check,s->arena))return false;
 if(!read_raw(s,s->arena->first+8,s->sector,1)||!write_checked(s,s->target,s->sector)){s->ready=false;return false;}return true;
}

static bool target_read(void*c,uint32_t at,void*b,uint32_t n){
 publication422*s=c;if(!s->ready||!s->state||!allowed(s)||!n)return false;
 if(s->target<at||s->target-at>=n)return read_raw(s,at,b,n);
 uint8_t*p=b;for(unsigned i=0;i<n;i++)if(!read_raw(s,at+i==s->target?s->arena->first+8:at+i,p+i*512,1))return false;return true;
}
pool_io publication422_target_io(publication422*s){pool_io io=publication422_io(s);if(!s||!s->state)return(pool_io){0};io.read=target_read;return io;}
