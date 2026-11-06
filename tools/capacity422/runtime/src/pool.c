#include "pool.h"
#include <string.h>
#include <limits.h>
static uint32_t get32(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(uint8_t *p,uint32_t x){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(x>>(i*8));}
uint32_t pool_crc(uint32_t crc,const void *data,uint32_t n){
 const uint8_t *p=data;
 static const uint32_t table[16]={0x00000000,0x1db71064,0x3b6e20c8,0x26d930ac,
 0x76dc4190,0x6b6b51f4,0x4db26158,0x5005713c,0xedb88320,0xf00f9344,
 0xd6d6a3e8,0xcb61b38c,0x9b64c2b0,0x86d3d2d4,0xa00ae278,0xbdbdf21c};
 while(n--){crc^=*p++;crc=(crc>>4)^table[crc&15];crc=(crc>>4)^table[crc&15];}
 return crc;
}
static bool referenced(const pool_catalog *c,unsigned id){
 if(c->control==id+1)return true;
 for(unsigned i=0;i<SP1_POOL_REFS;i++)if(c->ref[i]==id||c->annotation[i]==id+1)return true;
 return false;
}
/* Persistent lanes: owner low7=track reference+1, highbit=selected; order1..64.
 * Metadata is part of the same CRC-protected alternating catalog transaction. */
static bool lanes_valid(const pool_catalog *c){
 for(unsigned i=0;i<64;i++){
  unsigned lane=c->lane[i]&127,rank=c->rank[i];
  if(lane>64||rank>64||(!lane&&(rank||c->lane[i]))|| (!!lane!=!!rank) || (lane&&c->ref[64+i]==SP1_POOL_NONE))return false;
  for(unsigned j=0;j<i;j++)if(lane&&(c->lane[j]&127)==lane){
   if(c->rank[j]==rank || ((c->lane[i]&128)&&(c->lane[j]&128)))return false;
  }
 }
 return true;
}
int pool_lane_selected(const pool_catalog *c,unsigned track){
 if(!c||track>=64)return -1;int latest=-1;
 for(unsigned i=0;i<64;i++)if((c->lane[i]&127)==track+1 && c->ref[64+i]!=SP1_POOL_NONE){
  if(c->lane[i]&128)return (int)i;
  if(latest<0||c->rank[i]>c->rank[latest])latest=(int)i;
 }return latest;
}
bool pool_lane_select(pool_store *s,unsigned track,unsigned slot){
 if(!s||!s->ready||s->recording||track>=64||slot>=64||s->current.ref[64+slot]==SP1_POOL_NONE||(s->current.lane[slot]&127)!=track+1)return false;
 for(unsigned i=0;i<64;i++)if((s->current.lane[i]&127)==track+1)s->current.lane[i]&=127;
 s->current.lane[slot]|=128;return true;
}
int pool_lane_browse(const pool_catalog *c,unsigned track,int direction){
 int cur=pool_lane_selected(c,track);if(cur<0)return -1;
 int next=-1,wrap=-1;
 for(unsigned i=0;i<64;i++)if((c->lane[i]&127)==track+1){
  int rank=c->rank[i],old=c->rank[cur];
  if(wrap<0||(direction>0?rank<c->rank[wrap]:rank>c->rank[wrap]))wrap=i;
  if((direction>0?rank>old:rank<old)&&(next<0||(direction>0?rank<c->rank[next]:rank>c->rank[next])))next=i;
 }return next<0?wrap:next;
}
static void lane_append(pool_catalog *c,unsigned track,unsigned slot){
 /* Compact ranks before append so deleted slots never exhaust ordering. */
 uint8_t old[64];memcpy(old,c->rank,64);unsigned count=0;
 for(unsigned i=0;i<64;i++)if(i!=slot&&(c->lane[i]&127)==track+1){
  unsigned rank=1;for(unsigned j=0;j<64;j++)if(j!=slot&&(c->lane[j]&127)==track+1&&old[j]<old[i])rank++;
  c->rank[i]=rank;c->lane[i]&=127;count++;
 }
 c->lane[slot]=(track+1)|128;c->rank[slot]=count+1;
}
bool pool_encode(const pool_catalog *c,uint8_t b[SP1_POOL_CATALOG_BYTES]){
 if(!c || !b || !lanes_valid(c))return false;
 memset(b,0,SP1_POOL_CATALOG_BYTES);memcpy(b,"SP1POOL2",8);put32(b+8,4);
 put32(b+12,(uint32_t)c->generation);put32(b+16,(uint32_t)(c->generation>>32));
 if(c->control>SP1_POOL_OBJECTS)return false;
 put32(b+20,c->control);
 for(unsigned i=0;i<SP1_POOL_OBJECTS;i++){
  uint8_t *p=b+32+i*16;const pool_object *o=&c->object[i];
  put32(p,o->first);put32(p+4,o->blocks);put32(p+8,o->crc);put32(p+12,o->tag);
 }
 for(unsigned i=0;i<SP1_POOL_REFS;i++){
  uint16_t r=c->ref[i];if(r!=SP1_POOL_NONE && r>=SP1_POOL_OBJECTS)return false;
  b[2080+i*2]=(uint8_t)r;b[2081+i*2]=(uint8_t)(r>>8);
  unsigned a=c->annotation[i];if(a>SP1_POOL_OBJECTS||(a&&r==SP1_POOL_NONE))return false;
  b[2336+i*2]=(uint8_t)a;b[2337+i*2]=(uint8_t)(a>>8);
 }
 memcpy(b+2592,c->lane,64);memcpy(b+2656,c->rank,64);
 put32(b+4092,~pool_crc(UINT32_MAX,b,4092));return true;
}
bool pool_decode(const uint8_t b[SP1_POOL_CATALOG_BYTES],pool_catalog *c){
 if(memcmp(b,"SP1POOL2",8)||(get32(b+8)!=2&&get32(b+8)!=3&&get32(b+8)!=4)||get32(b+4092)!=~pool_crc(UINT32_MAX,b,4092))return false;
 for(unsigned i=24;i<32;i++)if(b[i])return false;
 for(unsigned i=get32(b+8)==4?2720:(get32(b+8)==3?2592:2336);i<4092;i++)if(b[i])return false;
 memset(c,0,sizeof(*c));c->generation=get32(b+12)|(uint64_t)get32(b+16)<<32;
 c->control=get32(b+20);if(c->control>SP1_POOL_OBJECTS)return false;
 for(unsigned i=0;i<SP1_POOL_OBJECTS;i++){
  const uint8_t *p=b+32+i*16;
  c->object[i]=(pool_object){get32(p),get32(p+4),get32(p+8),get32(p+12)};
 }
 if(c->control&&!c->object[c->control-1].blocks)return false;
 for(unsigned i=0;i<SP1_POOL_REFS;i++){
  uint16_t r=b[2080+i*2]|(uint16_t)b[2081+i*2]<<8;
  if(r!=SP1_POOL_NONE && (r>=SP1_POOL_OBJECTS||!c->object[r].blocks))return false;
  c->ref[i]=r;
  unsigned a=get32(b+8)>=3?(b[2336+i*2]|(unsigned)b[2337+i*2]<<8):0;
  if(a>SP1_POOL_OBJECTS||(a&&(r==SP1_POOL_NONE||!c->object[a-1].blocks)))return false;
  c->annotation[i]=a;
 }
 if(get32(b+8)==4){memcpy(c->lane,b+2592,64);memcpy(c->rank,b+2656,64);}
 return lanes_valid(c);
}
static bool overlap(uint32_t a,uint32_t n,uint32_t b,uint32_t m){
 return a<=b?b-a<n:a-b<m;
}
static bool geometry(pool_io io){
 if(!io.read||!io.write||!io.sync||!io.pool_blocks||io.pool_first>=io.device_blocks||
    io.pool_blocks>io.device_blocks-io.pool_first)return false;
 for(unsigned i=0;i<2;i++)if(io.catalog[i]>=io.device_blocks||
  8>io.device_blocks-io.catalog[i]||overlap(io.catalog[i],8,io.pool_first,io.pool_blocks))return false;
 return !overlap(io.catalog[0],8,io.catalog[1],8);
}
static bool valid(pool_store *s,pool_catalog *c){
 for(unsigned i=0;i<SP1_POOL_OBJECTS;i++){
  pool_object *o=&c->object[i];if(!o->blocks)continue;
  if(o->first>=s->io.pool_blocks || o->blocks>s->io.pool_blocks-o->first)return false;
  for(unsigned j=0;j<i;j++)if(c->object[j].blocks &&
    overlap(o->first,o->blocks,c->object[j].first,c->object[j].blocks))return false;
 }
 return true;
}
static bool read_catalog(pool_store *s,unsigned i,pool_catalog *c){
 return s->io.read(s->io.context,s->io.catalog[i],s->bytes,8)&&pool_decode(s->bytes,c)&&valid(s,c);
}
bool pool_open(pool_store *s,pool_io io){
 if(!s)return false;
 s->ready=false;
 if(!geometry(io))return false;
 memset(s,0,sizeof(*s));s->io=io;
 bool a=read_catalog(s,0,&s->current),b=read_catalog(s,1,&s->previous);
 if(!a&&!b)return false;
 if(a&&b&&s->current.generation==s->previous.generation &&
    memcmp(&s->current,&s->previous,sizeof(pool_catalog)))return false;
 if(!a||(b&&s->previous.generation>s->current.generation)){
  uint8_t *a_bytes=(uint8_t *)&s->current,*b_bytes=(uint8_t *)&s->previous;
  for(unsigned i=0;i<sizeof(pool_catalog);i++){uint8_t t=a_bytes[i];a_bytes[i]=b_bytes[i];b_bytes[i]=t;}
  s->active=1;s->have_previous=a;
 }else{s->active=0;s->have_previous=b;}
 s->ready=true;return true;
}
static bool persist(pool_store *s,unsigned slot,const pool_catalog *c){
 if(!pool_encode(c,s->bytes))return false;
 if(!s->io.write(s->io.context,s->io.catalog[slot],s->bytes,8)||!s->io.sync(s->io.context))return false;
 /* Read back exact encoded bytes using caller-independent512byte scratch. */
 uint8_t check[512];
 for(unsigned i=0;i<8;i++)if(!s->io.read(s->io.context,s->io.catalog[slot]+i,check,1)||
    memcmp(check,s->bytes+i*512,512))return false;
 return true;
}
bool pool_initialize(pool_store *s,pool_io io){
 if(!s)return false;
 s->ready=false;
 if(!geometry(io))return false;
 memset(s,0,sizeof(*s));s->io=io;
 for(unsigned i=0;i<SP1_POOL_REFS;i++)s->current.ref[i]=SP1_POOL_NONE;
 if(!persist(s,0,&s->current)||!persist(s,1,&s->current))return false;
 s->previous=s->current;s->have_previous=true;s->ready=true;return true;
}
static bool commit(pool_store *s){
 if(s->current.generation==UINT64_MAX){s->ready=false;return false;}
 s->current.generation++;
 /* Inactive disk checkpoint is replaced only after audio data is durable.
  * Failure poisons this session: caller must reopen, never blindly retry. */
 unsigned target=s->active^1u;
 if(!persist(s,target,&s->current)){s->ready=false;return false;}
 s->active=target;
 /* Previous is the old active checkpoint, reload rather than stack copy. */
 if(!read_catalog(s,target^1u,&s->previous)){s->ready=false;return false;}
 s->have_previous=true;return true;
}
bool pool_stage_stash(pool_store *s,uint16_t track,uint16_t stash){
 if(!s->ready||s->recording||track>=64||stash>=64)return false;
 unsigned slot=stash;stash+=64;if(s->current.ref[track]==SP1_POOL_NONE||s->current.ref[stash]!=SP1_POOL_NONE)return false;
 s->current.ref[stash]=s->current.ref[track];s->current.ref[track]=SP1_POOL_NONE;
 s->current.annotation[stash]=s->current.annotation[track];s->current.annotation[track]=0;lane_append(&s->current,track,slot);return true;
}
bool pool_stage_recall(pool_store *s,uint16_t stash,uint16_t track){
 if(!s->ready||s->recording||track>=64||stash>=64)return false;
 stash+=64;if(s->current.ref[stash]==SP1_POOL_NONE||s->current.ref[track]!=SP1_POOL_NONE)return false;
 s->current.ref[track]=s->current.ref[stash];s->current.annotation[track]=s->current.annotation[stash];return true;
}
bool pool_stash(pool_store *s,uint16_t track,uint16_t stash){
 return pool_stage_stash(s,track,stash)&&commit(s);
}
bool pool_recall(pool_store *s,uint16_t stash,uint16_t track){
 return pool_stage_recall(s,stash,track)&&commit(s);
}
bool pool_stage_drop(pool_store *s,uint16_t ref){
 if(!s||!s->ready||s->recording||ref>=SP1_POOL_REFS||s->current.ref[ref]==SP1_POOL_NONE)return false;
 if(!read_catalog(s,s->active,&s->previous)){s->ready=false;return false;}
 s->have_previous=true;
 s->current.ref[ref]=SP1_POOL_NONE;s->current.annotation[ref]=0;if(ref>=64){s->current.lane[ref-64]=0;s->current.rank[ref-64]=0;}return true;
}
bool pool_drop(pool_store *s,uint16_t ref){return pool_stage_drop(s,ref)&&commit(s);}
bool pool_record_begin(pool_store *s,uint32_t blocks){
 return pool_record_begin_aligned(s,blocks,1);
}
static bool find_extent(const pool_store *s,uint32_t blocks,uint32_t alignment,
 uint32_t extra_first,uint32_t extra_blocks,uint32_t *result){
 if(!blocks||blocks>s->io.pool_blocks||!alignment||(alignment&(alignment-1)))return false;
 uint32_t first=(0u-s->io.pool_first)&(alignment-1);
 if(first>s->io.pool_blocks-blocks)return false;
 for(;;){
  uint32_t next=first;
  for(unsigned g=0;g<2;g++){
   if(g&&!s->have_previous)continue;
   const pool_catalog *c=g?&s->previous:&s->current;
   for(unsigned i=0;i<SP1_POOL_OBJECTS;i++){
    const pool_object *o=&c->object[i];if(!referenced(c,i))continue;
    if(overlap(first,blocks,o->first,o->blocks)&&o->first+o->blocks>next)next=o->first+o->blocks;
   }
  }
  if(extra_blocks&&overlap(first,blocks,extra_first,extra_blocks)&&extra_first+extra_blocks>next)next=extra_first+extra_blocks;
  if(next==first){*result=first;return true;}
  uint32_t pad=(0u-(s->io.pool_first+next))&(alignment-1);
  if(pad>UINT32_MAX-next)return false;
  first=next+pad;if(first>s->io.pool_blocks-blocks)return false;
 }
}
unsigned pool_record_reason(const pool_store *s,uint32_t blocks,uint32_t alignment,uint32_t following){
 if(!s||!s->ready)return 64;
 if(s->recording)return 65;
 if(!blocks||!alignment||(alignment&(alignment-1)))return 66;
 unsigned free_ids=0;
 for(unsigned i=0;i<SP1_POOL_OBJECTS;i++)if(!referenced(&s->current,i)&&(!s->have_previous||!referenced(&s->previous,i)))free_ids++;
 if(free_ids<(following?2u:1u))return 67;
 uint32_t first,next;
 if(!find_extent(s,blocks,alignment,0,0,&first))return 68;
 if(following&&!find_extent(s,following,1,first,blocks,&next))return 69;
 return 0;
}
bool pool_record_room(const pool_store *s,uint32_t blocks,uint32_t alignment,uint32_t following){
 return pool_record_reason(s,blocks,alignment,following)==0;
}
bool pool_writer_room(const pool_store*s,uint32_t blocks,uint32_t alignment,uint32_t following,pool_writer*w){
 unsigned reason=pool_record_reason(s,blocks,alignment,following);
 if(reason&&w&&!(w->fault&0x80000000u))w->fault=(w->fault&0x00ffffffu)|(reason<<24);
 return reason==0;
}
bool pool_record_begin_aligned(pool_store *s,uint32_t blocks,uint32_t alignment){
 if(!s||!s->ready||s->recording)return false;
 unsigned id=SP1_POOL_OBJECTS;
 for(unsigned i=0;i<SP1_POOL_OBJECTS;i++)if(!referenced(&s->current,i)&&(!s->have_previous||!referenced(&s->previous,i))){id=i;break;}
 uint32_t first;
 if(id==SP1_POOL_OBJECTS||!find_extent(s,blocks,alignment,0,0,&first))return false;
 s->reserved_first=first;s->reserved_blocks=blocks;s->written=0;
 s->crc=UINT32_MAX;s->recording_object=(uint16_t)id;s->recording=true;return true;
}
bool pool_record_append(pool_store *s,const void *data,uint32_t blocks){
 if(!s->ready||!s->recording||!data||!blocks||blocks>s->reserved_blocks-s->written||blocks>UINT32_MAX/512)return false;
 if(!s->io.write(s->io.context,s->io.pool_first+s->reserved_first+s->written,data,blocks)){
  s->ready=false;return false;
 }
 s->crc=pool_crc(s->crc,data,blocks*512);s->written+=blocks;return true;
}
bool pool_record_finish(pool_store *s,uint16_t dest,uint32_t tag,bool replace){
 if(!s->ready||!s->recording||!s->written||dest>=SP1_POOL_REFS||
    (!replace&&s->current.ref[dest]!=SP1_POOL_NONE))return false;
 if(!s->io.sync(s->io.context)){s->ready=false;return false;}
 /* Remove only unreferenced current descriptors. Previous checkpoint still
  * pins its own copy; allocator considers both until the next commit. */
 for(unsigned i=0;i<SP1_POOL_OBJECTS;i++)if(!referenced(&s->current,i))memset(&s->current.object[i],0,sizeof(pool_object));
 s->current.object[s->recording_object]=(pool_object){s->reserved_first,s->written,~s->crc,tag};
 s->current.ref[dest]=s->recording_object;s->current.annotation[dest]=0;s->recording=false;
 return commit(s);
}
void pool_record_abort(pool_store *s){s->recording=false;}
bool pool_read_ref(pool_store *s,uint16_t ref,uint32_t offset,void *out,uint32_t blocks){
 if(!s->ready||ref>=SP1_POOL_REFS||!out||!blocks)return false;
 uint16_t id=s->current.ref[ref];if(id==SP1_POOL_NONE)return false;
 pool_object o=s->current.object[id];if(offset>=o.blocks||blocks>o.blocks-offset)return false;
 return s->io.read(s->io.context,s->io.pool_first+o.first+offset,out,blocks);
}
bool pool_verify_ref(pool_store *s,uint16_t ref){
 if(!s->ready||ref>=SP1_POOL_REFS)return false;
 uint16_t id=s->current.ref[ref];if(id==SP1_POOL_NONE)return false;
 uint32_t crc=UINT32_MAX;
 for(uint32_t i=0;i<s->current.object[id].blocks;i++){
  if(!pool_read_ref(s,ref,i,s->bytes,1))return false;
  crc=pool_crc(crc,s->bytes,512);
 }
 return ~crc==s->current.object[id].crc;
}

bool pool_prepare_writer(pool_store *s,uint32_t blocks,pool_writer *w){
 return pool_prepare_writer_aligned(s,blocks,1,w);
}
bool pool_prepare_writer_aligned(pool_store *s,uint32_t blocks,uint32_t alignment,pool_writer *w){
 if(!w || !pool_record_begin_aligned(s,blocks,alignment))return false;
 *w=(pool_writer){s->current.generation,s->reserved_first,s->reserved_blocks,0,
                  UINT32_MAX,s->recording_object,true,false};
 s->recording=false;return true;
}
bool pool_writer_append(pool_io io,pool_writer *w,const void *data,uint32_t blocks){
 if(!w||!w->active||w->failed||!data||!blocks||blocks>UINT32_MAX/512||
    w->written>w->capacity||blocks>w->capacity-w->written||!geometry(io)||
    w->first>=io.pool_blocks||w->capacity>io.pool_blocks-w->first)return false;
 if(!io.write(io.context,io.pool_first+w->first+w->written,data,blocks)){
  w->failed=true;return false;
 }
 w->crc=pool_crc(w->crc,data,blocks*512);w->written+=blocks;return true;
}
bool pool_finish_writer(pool_store *s,pool_writer *w,uint16_t dest,uint32_t tag,bool replace){
 if(!s||!w||!w->active||w->failed||!s->ready||s->recording||!w->written||
    w->generation!=s->current.generation||w->object>=SP1_POOL_OBJECTS)return false;
 s->reserved_first=w->first;s->reserved_blocks=w->capacity;s->written=w->written;
 s->crc=w->crc;s->recording_object=w->object;s->recording=true;
 bool ok=pool_record_finish(s,dest,tag,replace);
 if(ok)w->active=false;
 else {w->failed=true;s->ready=false;}
 return ok;
}

static bool stage(pool_store *s,pool_writer *w,uint32_t tag){
 if(!s||!w||!s->ready||s->recording||!w->active||w->failed||!w->written||
    w->generation!=s->current.generation||w->object>=SP1_POOL_OBJECTS||
    referenced(&s->current,w->object))return false;
 if(!s->io.sync(s->io.context)){s->ready=false;w->failed=true;return false;}
 /* Once RAM references change, allocations must still pin the CURRENT disk
  * checkpoint, not merely its predecessor. Otherwise a replacement can free
  * the old take before its new control/catalog transaction is durable. Reload
  * into the existing predecessor workspace; no extra retained catalog. */
 if(!read_catalog(s,s->active,&s->previous)){s->ready=false;w->failed=true;return false;}
 s->have_previous=true;
 for(unsigned i=0;i<SP1_POOL_OBJECTS;i++)if(!referenced(&s->current,i))memset(&s->current.object[i],0,sizeof(pool_object));
 s->current.object[w->object]=(pool_object){w->first,w->written,~w->crc,tag};
 w->active=false;return true;
}
bool pool_stage_writer(pool_store *s,pool_writer *w,uint16_t dest,uint32_t tag,bool replace){
 if(!s||dest>=SP1_POOL_REFS||(!replace&&s->current.ref[dest]!=SP1_POOL_NONE)||!stage(s,w,tag))return false;
 s->current.ref[dest]=w->object;s->current.annotation[dest]=0;return true;
}
bool pool_finish_control(pool_store *s,pool_writer *w,uint32_t tag){
 if(!stage(s,w,tag))return false;
 s->current.control=w->object+1;return commit(s);
}
bool pool_read_control(pool_store *s,uint32_t offset,void *out,uint32_t n){
 if(!s||!s->ready||!s->current.control||!out||!n)return false;
 pool_object o=s->current.object[s->current.control-1];
 if(offset>=o.blocks||n>o.blocks-offset)return false;
 return s->io.read(s->io.context,s->io.pool_first+o.first+offset,out,n);
}

uint32_t pool_crc_shift(uint32_t delta,uint64_t suffix){
 /* Reflected polynomial operators for 2^i zero bytes, for all 64 suffix bits.
  * The identity is 0x80000000; operator[0] advances it eight CRC bits and each
  * subsequent operator is its predecessor squared modulo 0xedb88320.
  * Keep the 256-byte table const: no retained RAM or per-call matrix arrays. */
 static const uint32_t operator[64]={
  0x00800000u,0x00008000u,0xedb88320u,0xb1e6b092u,
  0xa06a2517u,0xed627daeu,0x88d14467u,0xd7bbfe6au,
  0xec447f11u,0x8e7ea170u,0x6427800eu,0x4d47bae0u,
  0x09fe548fu,0x83852d0fu,0x30362f1au,0x7b5a9cc3u,
  0x31fec169u,0x9fec022au,0x6c8dedc4u,0x15d6874du,
  0x5fde7a4eu,0xbad90e37u,0x2e4e5eefu,0x4eaba214u,
  0xa8a472c0u,0x429a969eu,0x148d302au,0xc40ba6d0u,
  0xc4e22c3cu,0x40000000u,0x20000000u,0x08000000u,
  0x00800000u,0x00008000u,0xedb88320u,0xb1e6b092u,
  0xa06a2517u,0xed627daeu,0x88d14467u,0xd7bbfe6au,
  0xec447f11u,0x8e7ea170u,0x6427800eu,0x4d47bae0u,
  0x09fe548fu,0x83852d0fu,0x30362f1au,0x7b5a9cc3u,
  0x31fec169u,0x9fec022au,0x6c8dedc4u,0x15d6874du,
  0x5fde7a4eu,0xbad90e37u,0x2e4e5eefu,0x4eaba214u,
  0xa8a472c0u,0x429a969eu,0x148d302au,0xc40ba6d0u,
  0xc4e22c3cu,0x40000000u,0x20000000u,0x08000000u
 };
 for(unsigned i=0;suffix;i++,suffix>>=1){
  if(suffix&1){
   uint32_t op=operator[i],v=delta,out=0;
   for(unsigned bit=0;bit<32;bit++){
    if(op&0x80000000u)out^=v;
    op<<=1;v=(v>>1)^((v&1)?0xedb88320u:0);
   }
   delta=out;
  }
 }
 return delta;
}

bool pool_stage_annotation(pool_store *s,pool_writer *w,uint16_t ref,uint32_t tag){
 if(!s||!w||ref>=SP1_POOL_REFS||s->current.ref[ref]==SP1_POOL_NONE||w->written!=1||!stage(s,w,tag))return false;
 s->current.annotation[ref]=w->object+1;return true;
}
bool pool_read_annotation(pool_store *s,uint16_t ref,void *sector){
 if(!s||!s->ready||!sector||ref>=SP1_POOL_REFS||!s->current.annotation[ref])return false;
 pool_object o=s->current.object[s->current.annotation[ref]-1];
 return o.blocks==1&&s->io.read(s->io.context,s->io.pool_first+o.first,sector,1)&&~pool_crc(UINT32_MAX,sector,512)==o.crc;
}

bool pool_lane_commit(pool_store *s){return s&&s->ready&&!s->recording&&commit(s);}
bool pool_lane_claim(pool_store *s,unsigned track,unsigned slot){
 if(!s||!s->ready||s->recording||track>=64||slot>=64||s->current.ref[64+slot]==SP1_POOL_NONE||s->current.lane[slot])return false;
 lane_append(&s->current,track,slot);return true;
}
