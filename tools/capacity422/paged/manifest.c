#ifdef SP1_MAINTENANCE_CRC504
#include "../../import504/crc.h"
#endif
#include "manifest.h"
#include <string.h>
static uint32_t get(const uint8_t*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=n>>(8*i);}
/* Same IEEE CRC and on-disk bytes; two nibble steps replace eight bit steps. */
static uint32_t crc(const uint8_t*p){
#ifdef SP1_MAINTENANCE_CRC504
 return ~sp1_crc504(~0u,p,508);
#else
 static const uint32_t table[16]={0x00000000,0x1db71064,0x3b6e20c8,0x26d930ac,0x76dc4190,0x6b6b51f4,0x4db26158,0x5005713c,0xedb88320,0xf00f9344,0xd6d6a3e8,0xcb61b38c,0x9b64c2b0,0x86d3d2d4,0xa00ae278,0xbdbdf21c};
 uint32_t c=~0u;for(unsigned i=0;i<508;i++){c^=p[i];c=(c>>4)^table[c&15];c=(c>>4)^table[c&15];}return ~c;
#endif
}
static bool valid(const uint8_t*p){return get(p+508)==crc(p);}
static bool write_checked(cat422_manifest*s,uint32_t at){
 uint8_t check[512];put(s->sector+508,crc(s->sector));
 if(!s->io.write(s->io.ctx,at,s->sector)||(at==0&&!s->io.sync(s->io.ctx))||!s->io.read(s->io.ctx,at,check)||memcmp(check,s->sector,512)){s->building=false;s->ready=false;return false;}return true;
}
bool cat422_manifest_begin(cat422_manifest*s,pg422_io io,uint64_t gen){
 if(!s||!io.read||!io.write||!io.sync||io.sectors<2||!gen)return false;
 memset(s,0,sizeof *s);s->io=io;s->generation=gen;
 /* Invalidate any previous spool before overwriting its records. */
 if(!io.write(io.ctx,0,s->sector)||!io.sync(io.ctx))return false;
 uint8_t check[512];if(!io.read(io.ctx,0,check)||memcmp(check,s->sector,512))return false;
 s->building=true;return true;
}
static bool flush(cat422_manifest*s){
 if(!s->used)return true;
 put(s->sector,0x324d3443u);put(s->sector+4,(s->count-1)/15);put(s->sector+8,s->used);
 if(!write_checked(s,1+(s->count-1)/15))return false;
 memset(s->sector,0,512);s->used=0;return true;
}
bool cat422_manifest_append(cat422_manifest*s,const uint8_t b[32]){
 if(!s||!b||!s->building)return false;
 if(s->count>=PG422_LIMIT||s->count/15>=s->io.sectors-1){s->building=false;return false;}
 memcpy(s->sector+16+s->used*32,b,32);s->used++;s->count++;
 return s->used<15||flush(s);
}
bool cat422_manifest_finish(cat422_manifest*s){
 if(!s||!s->building||!flush(s))return false;
 /*505: private spool pages were fully read back while the durable header
  * remained invalid. Flush ALL data before publishing the final header. */
 if(s->count&&!s->io.sync(s->io.ctx)){s->building=false;s->ready=false;return false;}
 memset(s->sector,0,512);memcpy(s->sector,"CAT422MF",8);put(s->sector+8,s->generation);put(s->sector+12,s->generation>>32);put(s->sector+16,s->count);put(s->sector+20,s->io.sectors);
 if(!write_checked(s,0))return false;s->building=false;s->ready=true;return true;
}
bool cat422_manifest_open(cat422_manifest*s,pg422_io io,uint64_t gen){
 if(!s||!io.read||io.sectors<2)return false;
 memset(s,0,sizeof *s);s->io=io;
 if(!io.read(io.ctx,0,s->sector)||memcmp(s->sector,"CAT422MF",8)||!valid(s->sector)||get(s->sector+20)!=io.sectors)return false;
 s->generation=get(s->sector+8)|((uint64_t)get(s->sector+12)<<32);s->count=get(s->sector+16);
 if(!gen||s->generation!=gen||s->count>PG422_LIMIT||(s->count+14)/15>io.sectors-1)return false;
 s->ready=true;return true;
}
bool cat422_manifest_get(cat422_manifest*s,uint32_t id,uint8_t b[32]){
 if(!s||!s->ready||!b||id>=s->count)return false;
 uint32_t page=id/15,expected=s->count-page*15;if(expected>15)expected=15;
 if(s->used!=page+1){
  if(!s->io.read(s->io.ctx,1+page,s->sector)||!valid(s->sector)||get(s->sector)!=0x324d3443u||get(s->sector+4)!=page||get(s->sector+8)!=expected){s->ready=false;return false;}
  s->used=page+1; /* reuse build cursor as read cache tag; exclusive session */
 }
 memcpy(b,s->sector+16+(id%15)*32,32);return true;
}

bool cat422_manifest_peek(cat422_manifest*s,uint32_t id,uint8_t b[32]){
 if(!s||!b||id>=s->count)return false;if(s->ready)return cat422_manifest_get(s,id,b);if(!s->building)return false;
 if(s->used&&id/15==(s->count-1)/15){memcpy(b,s->sector+16+(id%15)*32,32);return true;}
 uint8_t page[512];if(!s->io.read(s->io.ctx,1+id/15,page)||!valid(page)||get(page)!=0x324d3443u||get(page+4)!=id/15||get(page+8)!=15)return false;
 memcpy(b,page+16+(id%15)*32,32);return true;
}

bool cat422_manifest_patch(cat422_manifest*s,uint32_t id,const uint8_t b[32]){
 if(!s||!s->building||!b||id>=s->count)return false;
 if(s->used&&id/15==(s->count-1)/15){memcpy(s->sector+16+(id%15)*32,b,32);return true;}
 uint8_t page[512],check[512];uint32_t at=1+id/15;
 if(!s->io.read(s->io.ctx,at,page)||!valid(page)||get(page)!=0x324d3443u||get(page+4)!=id/15||get(page+8)!=15)return false;
 memcpy(page+16+(id%15)*32,b,32);put(page+508,crc(page));
 if(!s->io.write(s->io.ctx,at,page)||!s->io.read(s->io.ctx,at,check)||memcmp(page,check,512)){s->building=false;return false;}return true;
}
