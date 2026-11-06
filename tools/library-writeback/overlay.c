#ifdef SP1_MAINTENANCE_CRC504
#include "../import504/crc.h"
#endif
#include "overlay.h"
#include <string.h>
#include <limits.h>
#define OV_MAGIC 0x314f4c54u
#define OV_MAP 0x3150414du
static uint32_t get(const uint8_t*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=v>>(8*i);}
/* Same IEEE CRC32, four-bit table: no RAM table and identical disk format. */
static uint32_t crc(const uint8_t*p,unsigned n){
#ifdef SP1_MAINTENANCE_CRC504
 return ~sp1_crc504(~0u,p,n);
#else
 static const uint32_t t[16]={0,0x1db71064,0x3b6e20c8,0x26d930ac,0x76dc4190,0x6b6b51f4,0x4db26158,0x5005713c,0xedb88320,0xf00f9344,0xd6d6a3e8,0xcb61b38c,0x9b64c2b0,0x86d3d2d4,0xa00ae278,0xbdbdf21c};
 uint32_t c=~0u;while(n--){c^=*p++;c=(c>>4)^t[c&15];c=(c>>4)^t[c&15];}return ~c;
#endif
}
/* Negative lookup filter rebuilt only from validated checkpoints. Collisions
 * cause ordinary checked reads; never a false negative. Exclusive session
 * ownership is required, as for the existing next-slot allocator. */
static unsigned bit(uint32_t l){return (l*2654435761u)>>18;}
static void remember(overlay*s,uint32_t l){unsigned b=bit(l);s->present[b/8]|=1u<<(b%8);unsigned bucket=(l%s->io.buckets)%4096u;s->occupied[bucket/8]|=1u<<(bucket%8);}
static bool possible(overlay*s,uint32_t l){unsigned b=bit(l);return (s->present[b/8]>>(b%8))&1;}

static void seal(uint8_t*p){put(p+508,crc(p,508));}
static bool sealed(const uint8_t*p){return get(p+508)==crc(p,508);}
static bool geometry(ov_io i){return i.read&&i.write&&i.sync&&i.buckets&&i.buckets<=16384&&i.virtual_sectors&&i.first<i.device_blocks&&i.blocks<=i.device_blocks-i.first&&i.blocks>2+2*i.buckets;}
static void header(uint8_t*p,ov_io i){memset(p,0,512);put(p,OV_MAGIC);put(p+4,1);put(p+8,i.first);put(p+12,i.blocks);put(p+16,i.buckets);put(p+20,i.virtual_sectors);memcpy(p+24,i.session,32);seal(p);}
static uint32_t map_lba(overlay*s,uint32_t bucket,unsigned copy){return s->io.first+2+bucket*2+copy;}
static uint32_t data_lba(overlay*s,uint32_t slot){return s->io.first+2+2*s->io.buckets+slot;}
static bool map_valid(overlay*s,const uint8_t*p,uint32_t bucket){
 if(get(p)!=OV_MAP||get(p+4)!=bucket||get(p+16)>OV_ENTRIES||!sealed(p))return false;
 for(unsigned i=20;i<32;i++)if(p[i])return false;
 unsigned n=get(p+16);for(unsigned i=0;i<n;i++){const uint8_t*e=p+32+i*12;uint32_t l=get(e),slot=get(e+4);if(l>=s->io.virtual_sectors||l%s->io.buckets!=bucket||slot>=s->limit)return false;for(unsigned j=0;j<i;j++)if(get(p+32+j*12)==l)return false;}
 for(unsigned i=32+n*12;i<508;i++)if(p[i])return false;return true;
}
static uint64_t sequence(const uint8_t*p){return get(p+8)|((uint64_t)get(p+12)<<32);}
static int bucket_read(overlay*s,uint32_t bucket){
 /* An I/O failure is not evidence that a checkpoint is invalid. Never silently
  * choose an older valid map when the other copy could not be read. */
 if(!s->io.read(s->io.ctx,map_lba(s,bucket,0),s->pages[0],2))return -1;
 bool a=map_valid(s,s->pages[0],bucket),b=map_valid(s,s->pages[1],bucket);
 if(!a&&!b)return -1;
 if(a&&b&&sequence(s->pages[0])==sequence(s->pages[1])&&memcmp(s->pages[0],s->pages[1],512))return -1;
 return !a||(b&&sequence(s->pages[1])>sequence(s->pages[0]))?1:0;
}
__attribute__((weak,noinline)) void *wav_work469(size_t*bytes){*bytes=0;return NULL;}
/* The host session exclusively owns converter scratch during creation/reopen.
 * Batch eight checkpoint pairs without changing any persistent/RAM layout. */
static uint8_t *map_work571(overlay*s){size_t n=0;uint8_t*p=wav_work469(&n);if(!p||n<8192)return NULL;if((uintptr_t)p<(uintptr_t)s+sizeof *s&&(uintptr_t)s<(uintptr_t)p+8192)return NULL;return p;}
bool overlay_create(overlay*s,ov_io io){
 memset(s,0,sizeof(*s));if(!geometry(io))return false;s->io=io;s->limit=io.blocks-2-2*io.buckets;
 uint8_t*work=map_work571(s);unsigned batch=work?8:1;
 /* Header published LAST. Incomplete creation can never authorize a visit. */
 for(unsigned b=0;b<io.buckets;){unsigned n=io.buckets-b;if(n>batch)n=batch;uint8_t*p=work?work:(uint8_t*)s->pages;
  for(unsigned j=0;j<n;j++){uint8_t*q=p+j*1024;memset(q,0,1024);put(q,OV_MAP);put(q+4,b+j);seal(q);memcpy(q+512,q,512);}
  if(!io.write(io.ctx,map_lba(s,b,0),p,n*2))return false;b+=n;
 }
 if(!io.sync(io.ctx))return false;header(s->pages[0],io);memcpy(s->pages[1],s->pages[0],512);
 if(!io.write(io.ctx,io.first,s->pages[0],2)||!io.sync(io.ctx))return false;
 return overlay_open(s,io);
}
bool overlay_open(overlay*s,ov_io io){
 memset(s,0,sizeof(*s));if(!geometry(io))return false;s->io=io;s->limit=io.blocks-2-2*io.buckets;
 header(s->verify,io);if(!io.read(io.ctx,io.first,s->pages[0],2)||memcmp(s->pages[0],s->verify,512)||memcmp(s->pages[1],s->verify,512))return false;
 uint8_t*work=map_work571(s);unsigned batch=work?8:1;
 for(unsigned b=0;b<io.buckets;){unsigned n=io.buckets-b;if(n>batch)n=batch;uint8_t*p=work?work:(uint8_t*)s->pages;
  if(!io.read(io.ctx,map_lba(s,b,0),p,n*2))return false;
  for(unsigned j=0;j<n;j++){const uint8_t*a=p+j*1024,*z=a+512;bool av=map_valid(s,a,b+j),bv=map_valid(s,z,b+j);if(!av&&!bv)return false;if(av&&bv&&sequence(a)==sequence(z)&&memcmp(a,z,512))return false;const uint8_t*q=!av||(bv&&sequence(z)>sequence(a))?z:a;
   for(unsigned k=0;k<get(q+16);k++){remember(s,get(q+32+k*12));uint32_t slot=get(q+36+k*12);if(slot>=s->next)s->next=slot+1;}
  }b+=n;
 }s->ready=true;return true;
}
/*509: compact frozen map index, stored beyond the native encoder's8KiB
 * in caller-owned admission workspace. Every entry is built from the same
 * checked checkpoint selection; payload CRC validation remains on every read.
 * General decoders invalidate before borrowing their overlapping workspace. */
__attribute__((weak)) void *overlay509_workspace(size_t*bytes){*bytes=0;return NULL;}
#define INDEX509_MAGIC 0x38584449u
#define INDEX509_HEAD 80u
#define INDEX509_ROW 8u
#ifdef IMPORT469
#define OFFLINE509 __attribute__((section(".conv469.text.index509")))
#else
#define OFFLINE509
#endif
__attribute__((weak)) bool overlay509_enabled(void){return false;}
__attribute__((weak)) void overlay509_enable(bool enabled){(void)enabled;}
OFFLINE509 void overlay509_invalidate(void){overlay509_enable(false);size_t n=0;uint8_t*p=overlay509_workspace(&n);if(p&&n>=INDEX509_HEAD)put(p,0);}
typedef struct {const uint8_t*offsets,*rows;uint32_t count,buckets,covered,slot_bits;} index510_view;
static OFFLINE509 bool index510_open(overlay*s,index510_view*v){
 if(!overlay509_enabled())return false;size_t bytes=0;const uint8_t*h=overlay509_workspace(&bytes);
 if(!h||bytes<INDEX509_HEAD||get(h)!=INDEX509_MAGIC||get(h+8)!=s->next||get(h+12)!=s->io.first||get(h+16)!=s->io.buckets||get(h+20)!=s->io.virtual_sectors||memcmp(h+24,&s,sizeof s)||memcmp(h+32,s->io.session,32))return false;
 uint32_t n=get(h+4),buckets=s->io.buckets,table=(buckets+1u)*2u,covered=get(h+64),packed_bits=get(h+68),bits=packed_bits&31u,row=(packed_bits&256u)?7u:8u;
 if((packed_bits&~287u)||!buckets||buckets>16384||covered>buckets||!covered||!bits||bits>31||s->limit>(1u<<bits)||(s->io.virtual_sectors-1)/buckets>(UINT32_MAX>>bits))return false;
 if(table>bytes-INDEX509_HEAD||n>(bytes-INDEX509_HEAD-table)/row)return false;
 *v=(index510_view){h+INDEX509_HEAD,h+INDEX509_HEAD+table,n,buckets,covered,packed_bits};return true;
}
static OFFLINE509 bool index510_find(overlay*s,const index510_view*v,uint32_t l,uint32_t*slot,uint32_t*sum,bool*found){
 *found=false;uint32_t bucket=l%v->buckets,q=l/v->buckets;if(bucket>=v->covered)return false;const uint8_t*offsets=v->offsets;
 uint32_t lo=offsets[bucket*2]|(uint32_t)offsets[bucket*2+1]<<8,hi=offsets[bucket*2+2]|(uint32_t)offsets[bucket*2+3]<<8;
 if(lo>hi||hi>v->count||hi-lo>OV_ENTRIES)return false;
 for(uint32_t i=lo;i<hi;i++){const uint32_t bits=v->slot_bits&31u,row=(v->slot_bits&256u)?7u:8u;const uint8_t*r=v->rows+i*row;uint32_t packed=row==8?get(r):r[0]|(uint32_t)r[1]<<8|(uint32_t)r[2]<<16;if((packed>>bits)==q){*slot=packed&((1u<<bits)-1u);if(*slot>=s->limit)return false;*sum=get(r+row-4);*found=true;break;}}
 return true;
}
OFFLINE509 int overlay509_build(overlay*s){
 overlay509_invalidate();size_t bytes=0;uint8_t*h=overlay509_workspace(&bytes);
 if(!s||!s->ready||!h||bytes<INDEX509_HEAD||!s->io.buckets||s->io.buckets>16384||s->next<128)return 0;
 unsigned bits=1;while(bits<31&&s->limit>(1u<<bits))bits++;
 if(s->limit>(1u<<bits)||(s->io.virtual_sectors-1)/s->io.buckets>(UINT32_MAX>>bits))return 0;
 /*577: cover a bounded prefix rather than abandon every fast lookup when a
  * large session exceeds RAM. Slots and key quotients share32bits, retaining
  * the full payload CRC. Reserve the existing wide PCM input tail on hardware.
  * Buckets outside the validated prefix retain ordinary checked map reads. */
 size_t usable=bytes>65536?(((uintptr_t)h+bytes-17920)&~(uintptr_t)3)-(uintptr_t)h:bytes;
 uint32_t table=(s->io.buckets+1u)*2u;if(table>usable-INDEX509_HEAD)return 0;
 unsigned row=bits<=24?7u:8u;
rebuild586:;
 uint32_t capacity=(usable-INDEX509_HEAD-table)/row;if(capacity>65535)capacity=65535;
 uint8_t*offsets=h+INDEX509_HEAD,*rows=offsets+table;uint32_t n=0,covered=0;
 /* The encoder output prefix is idle while building. Borrow it only when
  * disjoint from this index; eight checkpoint pairs per physical read. */
 uint8_t*work=map_work571(s);unsigned window=UINT_MAX,window_end=0;
 if(work&&(uintptr_t)work<(uintptr_t)h+bytes&&(uintptr_t)h<(uintptr_t)work+8192)work=NULL;
 /* Preserve the already-validated bucket grouping. A dynamically packed quotient/slot
  * plus full CRC fit8bytes; a16-bit offset table avoids sorting and searching
  * unrelated keys. Neither the persistent format nor overlay struct changes. */
 for(unsigned b=0;b<s->io.buckets;b++){
  offsets[b*2]=n;offsets[b*2+1]=n>>8;
  if(!(s->occupied[(b%4096u)/8]&(1u<<(b%8)))){covered=b+1;continue;}uint8_t*p;
  if(work){
   if(b<window||b>=window_end){unsigned count=s->io.buckets-b;if(count>8)count=8;if(!s->io.read(s->io.ctx,map_lba(s,b,0),work,count*2))goto fail;window=b;window_end=b+count;}
   uint8_t*a=work+(b-window)*1024,*z=a+512;bool av=map_valid(s,a,b),bv=map_valid(s,z,b);
   if((!av&&!bv)||(av&&bv&&sequence(a)==sequence(z)&&memcmp(a,z,512)))goto fail;
   p=!av||(bv&&sequence(z)>sequence(a))?z:a;
  }else{int active=bucket_read(s,b);if(active<0)goto fail;p=s->pages[active];}
  /* Validate the whole bucket before publishing any compact rows. A high
   * quotient restarts in the exact eight-byte representation; no truncation. */
  if(row==7)for(unsigned i=0;i<get(p+16);i++){
   const uint8_t*e=p+32+i*12;
   if(((get(e)/s->io.buckets)<<bits)>0xffffffu){row=8;goto rebuild586;}
  }
  if(get(p+16)>capacity-n)break;
  covered=b+1;for(unsigned i=0;i<get(p+16);i++){const uint8_t*e=p+32+i*12;uint8_t*r=rows+n++*row;uint32_t q=get(e)/s->io.buckets,slot=get(e+4);put(r,(q<<bits)|slot);put(r+row-4,get(e+8));
  }
 }
 if(!covered||!n)return 0;offsets[covered*2]=n;offsets[covered*2+1]=n>>8;
 memset(h,0,INDEX509_HEAD);put(h+4,n);put(h+8,s->next);put(h+12,s->io.first);put(h+16,s->io.buckets);put(h+20,s->io.virtual_sectors);memcpy(h+24,&s,sizeof s);memcpy(h+32,s->io.session,32);put(h+64,covered);put(h+68,bits|(row==7?256u:0u));put(h,INDEX509_MAGIC);overlay509_enable(true);return 1;
fail:s->ready=false;return -1;
}
/*513: borrow only the unused end of the existing frozen-index workspace.
 * Canonical PCM never runs the general decoder; output prefix remains separate.
 * An active index must prove its whole descriptor/table/rows precede the borrow. */
OFFLINE509 void *overlay513_tail(uint32_t wanted){
 size_t bytes=0;uint8_t*h=overlay509_workspace(&bytes);
 if(!h||!wanted||bytes<wanted)return NULL;
 uintptr_t at=((uintptr_t)h+bytes-wanted)&~(uintptr_t)3;
 if(at<(uintptr_t)h||at-(uintptr_t)h>bytes-wanted)return NULL;
 if(overlay509_enabled()){
  if(bytes<INDEX509_HEAD||get(h)!=INDEX509_MAGIC)return NULL;
  uint32_t n=get(h+4),buckets=get(h+16);if(!buckets||buckets>16384||!get(h+64)||get(h+64)>buckets)return NULL;
  uint32_t packed_bits=get(h+68),row=(packed_bits&256u)?7u:8u;
  if((packed_bits&~287u)||!(packed_bits&31u))return NULL;
  uint32_t table=(buckets+1u)*2u;
  if(table>bytes-INDEX509_HEAD||n>(bytes-INDEX509_HEAD-table)/row)return NULL;
  size_t used=INDEX509_HEAD+table+(size_t)n*row;
  if(used>at-(uintptr_t)h)return NULL;
 }
 return (void*)at;
}
/* Cached diagnostic only, emitted after commit. Not recovery authority. */
OFFLINE509 uint32_t overlay509_count(overlay*s){index510_view v;return s&&index510_open(s,&v)?v.count:0;}

OFFLINE509 bool overlay_get(overlay*s,uint32_t l,void*out,bool*found){
 if(!s||!found)return false;*found=false;if(!s->ready||l>=s->io.virtual_sectors)return false;
 uint32_t cached_slot,cached_sum;bool cached_found;index510_view view;
 if(index510_open(s,&view)&&index510_find(s,&view,l,&cached_slot,&cached_sum,&cached_found)){
  if(!cached_found)return true;
  if(!s->io.read(s->io.ctx,data_lba(s,cached_slot),out,1)||crc(out,512)!=cached_sum){s->ready=false;return false;}*found=true;return true;
 }
 if(!possible(s,l))return true;
 int copy=bucket_read(s,l%s->io.buckets);if(copy<0){s->ready=false;return false;}uint8_t*p=s->pages[copy];
 for(unsigned i=0;i<get(p+16);i++){uint8_t*e=p+32+i*12;if(get(e)!=l)continue;uint32_t slot=get(e+4),sum=get(e+8);
  if(!s->io.read(s->io.ctx,data_lba(s,slot),out,1)||crc(out,512)!=sum){s->ready=false;return false;}*found=true;return true;
 }return true;
}
/*508: use the caller's output as temporary checkpoint scratch, then replace
 * it with verified payloads. Existing overlay scratch and static size unchanged.
 * Exclusive ownership prevents maps changing between validation and data reads. */
#ifdef IMPORT469
__attribute__((section(".conv469.text.overlay508")))
#endif
bool overlay_get_many(overlay*s,uint32_t l,void*out,uint32_t count,uint32_t*found){
 if(!s||!found)return false;*found=0;
 if(!s->ready||!out||count<2||count>16||l>=s->io.virtual_sectors||count>s->io.virtual_sectors-l)return false;
 uint32_t slots[16],sums[16],mask=0;uint8_t*bytes=out;
 index510_view view;bool indexed=index510_open(s,&view);if(indexed)for(unsigned i=0;i<count;i++){bool exists;if(!index510_find(s,&view,l+i,slots+i,sums+i,&exists)){indexed=false;mask=0;break;}if(exists)mask|=1u<<i;}
 if(!indexed)for(unsigned i=0;i<count;){
  if(!possible(s,l+i)){i++;continue;}
  unsigned bucket=(l+i)%s->io.buckets,n=0,limit=count/2;
  while(n<limit&&i+n<count&&bucket+n<s->io.buckets&&possible(s,l+i+n))n++;
  if(!s->io.read(s->io.ctx,map_lba(s,bucket,0),bytes,n*2))goto fail;
  for(unsigned j=0;j<n;j++){
   uint8_t*a=bytes+j*1024,*b=a+512;bool av=map_valid(s,a,bucket+j),bv=map_valid(s,b,bucket+j);
   if(!av&&!bv)goto fail;
   if(av&&bv&&sequence(a)==sequence(b)&&memcmp(a,b,512))goto fail;
   uint8_t*p=!av||(bv&&sequence(b)>sequence(a))?b:a;
   for(unsigned k=0;k<get(p+16);k++){const uint8_t*e=p+32+k*12;if(get(e)==l+i+j){slots[i+j]=get(e+4);sums[i+j]=get(e+8);mask|=1u<<(i+j);break;}}
  }i+=n;
 }
 for(unsigned i=0;i<count;){
  if(!(mask&(1u<<i))){i++;continue;}
  unsigned n=1;while(i+n<count&&(mask&(1u<<(i+n)))&&slots[i+n]==slots[i]+n)n++;
  if(!s->io.read(s->io.ctx,data_lba(s,slots[i]),bytes+i*512,n))goto fail;
  for(unsigned j=0;j<n;j++)if(crc(bytes+(i+j)*512,512)!=sums[i+j])goto fail;
  i+=n;
 }*found=mask;return true;
fail:s->ready=false;return false;
}
bool overlay_put(overlay*s,uint32_t l,const void*input){return overlay_put_many(s,l,input,1);}

bool overlay_put_many(overlay*s,uint32_t l,const void*input,uint32_t count){
 if(!s||!s->ready||!input||!count||count>OV_WRITE_BATCH||count>s->io.buckets||l>=s->io.virtual_sectors||count>s->io.virtual_sectors-l)return false;
 if(s->next>s->limit||count>s->limit-s->next)return false;
 const uint8_t*bytes=input;uint32_t targets[OV_WRITE_BATCH];
 /* Preflight every checkpoint and capacity before the first write. Consecutive
  * LBAs and count<=buckets guarantee no checkpoint is updated twice here. */
 for(unsigned j=0;j<count;j++){
  uint32_t bucket=(l+j)%s->io.buckets;int active=bucket_read(s,bucket);if(active<0)goto fail;
  const uint8_t*old=s->pages[active];unsigned n=get(old+16),index=n;
  for(unsigned i=0;i<n;i++)if(get(old+32+i*12)==l+j){index=i;break;}
  if(index>=OV_ENTRIES||sequence(old)==UINT64_MAX)return false;
  uint8_t*p=s->pages[active];uint8_t*e=p+32+index*12;uint64_t seq=sequence(old)+1;
  put(e,l+j);put(e+4,s->next+j);put(e+8,crc(bytes+j*512,512));put(p+16,index==n?n+1:n);put(p+8,seq);put(p+12,seq>>32);seal(p);targets[j]=map_lba(s,bucket,active^1u);if(j==0)memcpy(s->verify,p,512);else if(p!=s->pages[0])memcpy(s->pages[0],p,512);
 }
 if(!s->io.write(s->io.ctx,data_lba(s,s->next),bytes,count)||!s->io.sync(s->io.ctx))goto fail;
 /* verify holds map0, pages[0] holds map1, pages[1] is readback scratch. */
 for(unsigned j=0;j<count;j++)if(!s->io.read(s->io.ctx,data_lba(s,s->next+j),s->pages[1],1)||memcmp(s->pages[1],bytes+j*512,512))goto fail;
 for(unsigned j=0;j<count;j++)if(!s->io.write(s->io.ctx,targets[j],j?s->pages[0]:s->verify,1))goto fail;
 if(!s->io.sync(s->io.ctx))goto fail;
 for(unsigned j=0;j<count;j++)if(!s->io.read(s->io.ctx,targets[j],s->pages[1],1)||memcmp(s->pages[1],j?s->pages[0]:s->verify,512))goto fail;
 for(unsigned j=0;j<count;j++)remember(s,l+j);s->next+=count;return true;
fail:s->ready=false;return false;
}

#ifdef IMPORT469
__attribute__((section(".conv469.text.overlaywrite508")))
#endif
bool overlay_put_span(overlay*s,uint32_t l,const void*input,uint32_t count,void*scratch,uint32_t capacity){
 if(!s||!s->ready||!input||!scratch||capacity<16384||!count||count>16||count>s->io.buckets||l>=s->io.virtual_sectors||count>s->io.virtual_sectors-l)return false;
 if(s->next>s->limit||count>s->limit-s->next)return false;
 uint8_t*maps=scratch,*readback=maps+8192;const uint8_t*bytes=input;uint32_t targets[16];
 for(unsigned i=0;i<count;){
  unsigned bucket=(l+i)%s->io.buckets,n=count-i;if(n>8)n=8;if(n>s->io.buckets-bucket)n=s->io.buckets-bucket;
  if(!s->io.read(s->io.ctx,map_lba(s,bucket,0),readback,n*2))goto fail;
  for(unsigned j=0;j<n;j++){
   uint8_t*a=readback+j*1024,*b=a+512;bool av=map_valid(s,a,bucket+j),bv=map_valid(s,b,bucket+j);
   if(!av&&!bv)goto fail;if(av&&bv&&sequence(a)==sequence(b)&&memcmp(a,b,512))goto fail;
   unsigned active=!av||(bv&&sequence(b)>sequence(a));uint8_t*old=active?b:a;unsigned entries=get(old+16),index=entries;
   for(unsigned k=0;k<entries;k++)if(get(old+32+k*12)==l+i+j){index=k;break;}
   if(index>=OV_ENTRIES||sequence(old)==UINT64_MAX)return false;
   uint8_t*p=maps+(i+j)*512;memcpy(p,old,512);uint8_t*e=p+32+index*12;uint64_t seq=sequence(old)+1;
   put(e,l+i+j);put(e+4,s->next+i+j);put(e+8,crc(bytes+(i+j)*512,512));put(p+16,index==entries?entries+1:entries);put(p+8,seq);put(p+12,seq>>32);seal(p);targets[i+j]=map_lba(s,bucket+j,active^1u);
  }i+=n;
 }
 if(!s->io.write(s->io.ctx,data_lba(s,s->next),bytes,count)||!s->io.sync(s->io.ctx))goto fail;
 if(!s->io.read(s->io.ctx,data_lba(s,s->next),readback,count)||memcmp(readback,bytes,count*512))goto fail;
 for(unsigned i=0;i<count;i++)if(!s->io.write(s->io.ctx,targets[i],maps+i*512,1))goto fail;
 if(!s->io.sync(s->io.ctx))goto fail;
 /*575: verify inactive checkpoints in bounded contiguous read windows.
  * Active copies are only read, never rewritten. Exact comparisons and both
  * durable barriers remain; scratch is the existing8KiB readback half. */
 for(unsigned i=0;i<count;){
  unsigned n=1;while(i+n<count&&targets[i+n]>=targets[i]&&targets[i+n]-targets[i]<16)n++;
  unsigned span=targets[i+n-1]-targets[i]+1;
  if(!s->io.read(s->io.ctx,targets[i],readback,span))goto fail;
  for(unsigned j=0;j<n;j++)if(memcmp(readback+(targets[i+j]-targets[i])*512,maps+(i+j)*512,512))goto fail;
  i+=n;
 }
 for(unsigned i=0;i<count;i++)remember(s,l+i);s->next+=count;return true;
fail:s->ready=false;return false;
}

/* Read the checkpoint pair once for both comparison and preflight. Every
 * previous payload is still CRC checked, including identical host rewrites. */
#ifdef IMPORT469
__attribute__((section(".conv469.text.overlaywrite602")))
#endif
static const uint8_t *entry602(const uint8_t*p,uint32_t l){
 for(unsigned i=0;i<get(p+16);i++)if(get(p+32+i*12)==l)return p+32+i*12;
 return NULL;
}
#ifdef IMPORT469
__attribute__((section(".conv469.text.overlaywrite602")))
#endif
bool overlay_update_span(overlay*s,uint32_t l,const void*input,uint32_t count,void*scratch,uint32_t capacity,ov_read base,void*base_ctx){
 if(!s||!s->ready||!input||!scratch||!base||capacity<16384||!count||count>16||count>s->io.buckets||l>=s->io.virtual_sectors||count>s->io.virtual_sectors-l)return false;
 uint8_t*maps=scratch,*readback=maps+8192;const uint8_t*bytes=input;uint32_t targets[16];
 /* Preserve the existing zero-allocation fast path. The validated negative
  * filter proves that the immutable baseline supplies every sector here;
  * still compare that baseline, rather than assuming free space is zero. */
 bool absent=true;for(unsigned i=0;i<count;i++)if(possible(s,l+i)){absent=false;break;}
 if(absent){bool zero=true;for(unsigned i=0;i<count*512;i++)if(bytes[i]){zero=false;break;}
  if(zero){if(!base(base_ctx,l,readback,count))goto fail;if(!memcmp(bytes,readback,count*512))return true;}
 }
 for(unsigned i=0;i<count;){
  unsigned bucket=(l+i)%s->io.buckets,n=count-i;if(n>8)n=8;if(n>s->io.buckets-bucket)n=s->io.buckets-bucket;
  if(!s->io.read(s->io.ctx,map_lba(s,bucket,0),readback,n*2))goto fail;
  for(unsigned j=0;j<n;j++){
   uint8_t*a=readback+j*1024,*b=a+512;bool av=map_valid(s,a,bucket+j),bv=map_valid(s,b,bucket+j);
   if(!av&&!bv)goto fail;if(av&&bv&&sequence(a)==sequence(b)&&memcmp(a,b,512))goto fail;
   unsigned active=!av||(bv&&sequence(b)>sequence(a));
   memcpy(maps+(i+j)*512,active?b:a,512);targets[i+j]=map_lba(s,bucket+j,active^1u);
  }i+=n;
 }
 for(unsigned i=0;i<count;){
  const uint8_t*e=entry602(maps+i*512,l+i);unsigned n=1;
  if(e){
   uint32_t slot=get(e+4);
   while(i+n<count){const uint8_t*next=entry602(maps+(i+n)*512,l+i+n);if(!next||get(next+4)!=slot+n)break;n++;}
   if(!s->io.read(s->io.ctx,data_lba(s,slot),readback+i*512,n))goto fail;
   for(unsigned j=0;j<n;j++){const uint8_t*row=entry602(maps+(i+j)*512,l+i+j);if(crc(readback+(i+j)*512,512)!=get(row+8))goto fail;}
  }else{
   while(i+n<count&&!entry602(maps+(i+n)*512,l+i+n))n++;
   if(!base(base_ctx,l+i,readback+i*512,n))goto fail;
  }i+=n;
 }
 uint32_t changed=0;unsigned total=0;
 for(unsigned i=0;i<count;i++)if(memcmp(readback+i*512,bytes+i*512,512)){changed|=1u<<i;total++;}
 if(!total)return true;
 if(s->next>s->limit||total>s->limit-s->next)return false;
 unsigned packed=0;
 for(unsigned i=0;i<count;i++)if(changed&(1u<<i)){
  uint8_t*p=maps+i*512;const uint8_t*old=entry602(p,l+i);unsigned entries=get(p+16),index=old?(unsigned)(old-p-32)/12:entries;
  if(index>=OV_ENTRIES||sequence(p)==UINT64_MAX)return false;
  uint8_t*e=p+32+index*12;uint64_t seq=sequence(p)+1;
  put(e,l+i);put(e+4,s->next+packed);put(e+8,crc(bytes+i*512,512));put(p+16,index==entries?entries+1:entries);put(p+8,seq);put(p+12,seq>>32);seal(p);
  memcpy(readback+packed*512,bytes+i*512,512);packed++;
 }
 if(!s->io.write(s->io.ctx,data_lba(s,s->next),readback,total)||!s->io.sync(s->io.ctx))goto fail;
 if(!s->io.read(s->io.ctx,data_lba(s,s->next),readback,total))goto fail;
 packed=0;for(unsigned i=0;i<count;i++)if(changed&(1u<<i)){if(memcmp(readback+packed*512,bytes+i*512,512))goto fail;packed++;}
 for(unsigned i=0;i<count;i++)if(changed&(1u<<i))if(!s->io.write(s->io.ctx,targets[i],maps+i*512,1))goto fail;
 if(!s->io.sync(s->io.ctx))goto fail;
 for(unsigned i=0;i<count;){
  if(!(changed&(1u<<i))){i++;continue;}
  unsigned end=i+1;while(end<count&&targets[end]>=targets[i]&&targets[end]-targets[i]<16)end++;
  unsigned last=i;for(unsigned j=i+1;j<end;j++)if(changed&(1u<<j))last=j;
  unsigned span=targets[last]-targets[i]+1;
  if(!s->io.read(s->io.ctx,targets[i],readback,span))goto fail;
  for(unsigned j=i;j<=last;j++)if(changed&(1u<<j))if(memcmp(readback+(targets[j]-targets[i])*512,maps+j*512,512))goto fail;
  i=end;
 }
 for(unsigned i=0;i<count;i++)if(changed&(1u<<i))remember(s,l+i);
 s->next+=total;return true;
fail:s->ready=false;return false;
}

bool overlay_visit(overlay*s,ov_visit visit,void*ctx){
 if(!s||!s->ready||!visit)return false;
 /* Occupancy comes only from validated open checkpoints or durable puts.
  * Empty buckets cannot contribute entries; retain checked reads for occupied ones. */
 for(unsigned b=0;b<s->io.buckets;b++){if(!(s->occupied[(b%4096u)/8]&(1u<<(b%8))))continue;int c=bucket_read(s,b);if(c<0){s->ready=false;return false;}const uint8_t*p=s->pages[c];for(unsigned i=0;i<get(p+16);i++)if(!visit(ctx,get(p+32+i*12)))return false;}return true;
}
