#include "fresh.h"
#include <string.h>
#define PART 8192u
#define DATA 16384u
#define SPC 64u
static void put(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=n>>(8*i);}
static void put16(uint8_t*p,unsigned n){p[0]=n;p[1]=n>>8;}
static uint32_t crc(const uint8_t*p,unsigned n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;i++)c=(c>>1)^(0xedb88320u&-(c&1));}return ~c;}
bool fresh529_geometry_make(uint32_t capacity,fresh529_geometry*g){
 if(!g||capacity!=FRESH529_CAPACITY)return false;
 memset(g,0,sizeof *g);g->capacity=capacity;g->data=DATA;g->clusters=(capacity-DATA)/SPC;g->end=g->clusters+2;g->fat=(g->end*4+511)/512;
 if(g->fat*2>=8192||g->clusters<65525||g->clusters>0x0ffffff5)return false;g->reserved=8192-2*g->fat;
 uint32_t first=6,after=g->end-FRESH529_RESERVE/SPC-2*FRESH529_INDEX/SPC-FRESH529_WORK/SPC;
 if(after<=first||after-first>0xffffffffu/(SPC*512u))return false;
 g->audio_first=DATA+(first-2)*SPC;g->audio_blocks=(after-first)*SPC;
 g->index[0]=DATA+(after-2)*SPC;g->index[1]=g->index[0]+FRESH529_INDEX;g->work_first=g->index[1]+FRESH529_INDEX;
 return g->work_first+FRESH529_WORK+FRESH529_RESERVE<=DATA+g->clusters*SPC;
}
static void progress(fresh529_work*w,unsigned phase,uint32_t done,uint32_t total){if(w->io.progress)w->io.progress(w->io.ctx,phase,done,total);}
static bool write_verified(fresh529_work*w,uint32_t at,const void*b,uint32_t n){
 return n&&n<=16&&at<w->g.capacity&&n<=w->g.capacity-at&&w->io.write(w->io.ctx,at,b,n)&&w->io.read(w->io.ctx,at,w->check,n)&&!memcmp(b,w->check,n*512);
}
static bool zeros(fresh529_work*w,uint32_t at,uint32_t n){memset(w->sectors,0,sizeof w->sectors);while(n){unsigned k=n>16?16:n;if(!write_verified(w,at,w->sectors,k))return false;at+=k;n-=k;}return true;}
static uint32_t cluster(const fresh529_geometry*g,uint32_t lba){return (lba-g->data)/SPC+2;}
static uint32_t fat_entry(const fresh529_geometry*g,uint32_t c){
 if(c==0)return 0x0ffffff8;if(c>=1&&c<=5)return 0x0fffffff;
 uint32_t start[4]={6,cluster(g,g->index[0]),cluster(g,g->index[1]),cluster(g,g->work_first)};
 uint32_t count[4]={g->audio_blocks/SPC,FRESH529_INDEX/SPC,FRESH529_INDEX/SPC,FRESH529_WORK/SPC};
 for(unsigned i=0;i<4;i++)if(c>=start[i]&&c-start[i]<count[i])return c-start[i]==count[i]-1?0x0fffffff:c+1;
 return 0;
}
static void entry(uint8_t*p,const char*name,unsigned attr,uint32_t c,uint32_t bytes){memset(p,0,32);memcpy(p,name,11);p[11]=attr;put16(p+20,c>>16);put16(p+26,c);put(p+28,bytes);}
static void boot(fresh529_work*w){
 uint8_t*b=w->sectors;memset(b,0,512);b[0]=0xeb;b[1]=0x58;b[2]=0x90;memcpy(b+3,"SP1FS2  ",8);put16(b+11,512);b[13]=SPC;put16(b+14,w->g.reserved);b[16]=2;b[21]=0xf8;put16(b+24,63);put16(b+26,255);put(b+28,PART);put(b+32,w->g.capacity-PART);put(b+36,w->g.fat);put(b+44,2);put16(b+48,1);put16(b+50,6);b[64]=0x80;b[66]=0x29;put(b+67,0x53503104);memcpy(b+71,"SP1 MUSIC  ",11);memcpy(b+82,"FAT32   ",8);b[510]=0x55;b[511]=0xaa;
}
static bool ird(void*c,uint32_t at,void*b){fresh529_work*w=c;return at<FRESH529_INDEX&&w->io.read(w->io.ctx,w->index_first+at,b,1);}
static bool iwr(void*c,uint32_t at,const void*b){fresh529_work*w=c;return at<FRESH529_INDEX&&write_verified(w,w->index_first+at,b,1);}
static bool isync(void*c){fresh529_work*w=c;return w->io.sync(w->io.ctx);}
static void control(uint8_t*b){
 memset(b,0,3072);put(b,0x53453341);for(unsigned s=0;s<16;s++)put(b+8+s*44,65536);
 /* Empty X3 table, same canonical self-checking legacy control envelope. */
 put(b+1536,0x53453358);b[1540]=1;
}
static bool indexes(fresh529_work*w){
 control(w->sectors);uint32_t sum=crc(w->sectors,3072);if(!write_verified(w,w->g.audio_first,w->sectors,6))return false;
 w->index_first=w->g.index[0];pg422_io io={w,ird,iwr,isync,FRESH529_INDEX};
 if(!pg422_create(&w->index,io)||!pg422_begin(&w->index))return false;
 uint8_t b[32]={0};put(b,1);put(b+4,1);put(b+8,258);put(b+12,66);put(b+16,w->g.audio_blocks);
 if(!pg422_stage(&w->index,0,b))return false;
 memset(b,0,32);put(b,2);for(unsigned id=1;id<=65;id++)if(!pg422_stage(&w->index,id,b))return false;
 memset(b,0,32);put(b,3);put(b+8,6);put(b+12,sum);put(b+16,0x314c5443);put(b+20,1);
 if(!pg422_stage(&w->index,66,b)||!pg422_commit(&w->index))return false;
 /* Equal generations MUST have byte-identical reachable prefixes. Copy the
  * checked committed bank, not independently generated COW histories. */
 uint32_t tail=w->index.tail;
 for(uint32_t at=0;at<tail;at+=16){unsigned n=tail-at>16?16:tail-at;
  if(!w->io.read(w->io.ctx,w->g.index[0]+at,w->sectors,n)||!write_verified(w,w->g.index[1]+at,w->sectors,n))return false;}
 return true;
}
bool fresh529_initialize(fresh529_io io,uint32_t capacity,fresh529_work*w){
 if(!w||!io.read||!io.write||!io.sync)return false;
 memset(w,0,sizeof *w);w->io=io;if(!fresh529_geometry_make(capacity,&w->g))return false;
 /* Explicit destructive operation: invalidate activation FIRST and durably.
  * No whole-card erase; unused audio/work sectors are never scanned. */
 progress(w,1,0,1);if(!zeros(w,0,1)||!io.sync(io.ctx))return false;
 if(!zeros(w,PART,8))return false;
 boot(w);if(!write_verified(w,PART,w->sectors,1)||!write_verified(w,PART+6,w->sectors,1))return false;
 memset(w->sectors,0,512);put(w->sectors,0x41615252);put(w->sectors+484,0x61417272);
 uint32_t used=4+w->g.audio_blocks/SPC+2*FRESH529_INDEX/SPC+FRESH529_WORK/SPC;
 put(w->sectors+488,w->g.clusters-used);put(w->sectors+492,cluster(&w->g,w->g.work_first)+FRESH529_WORK/SPC);put(w->sectors+508,0xaa550000);
 if(!write_verified(w,PART+1,w->sectors,1)||!write_verified(w,PART+7,w->sectors,1))return false;
 for(unsigned copy=0;copy<2;copy++)for(uint32_t at=0;at<w->g.fat;at+=16){unsigned n=w->g.fat-at>16?16:w->g.fat-at;memset(w->sectors,0,n*512);
  for(unsigned i=0;i<n*128;i++)put(w->sectors+i*4,fat_entry(&w->g,at*128+i));
  if(!write_verified(w,PART+w->g.reserved+copy*w->g.fat+at,w->sectors,n))return false;progress(w,2,copy*w->g.fat+at+n,2*w->g.fat);}
 /* Root, owned directory, workspace epoch headers. Remaining WORK bytes are
  * unreachable until the editor creates a new epoch and overwrites them. */
 if(!zeros(w,DATA,256)||!zeros(w,w->g.work_first,64))return false;
 memset(w->sectors,0,512);entry(w->sectors,"SP1 MUSIC  ",8,0,0);entry(w->sectors+32,"TAPELOOP   ",16,3,0);
 if(!write_verified(w,DATA,w->sectors,1))return false;
 memset(w->sectors,0,512);entry(w->sectors,".          ",16,3,0);entry(w->sectors+32,"..         ",16,0,0);
 entry(w->sectors+64,"AUDIO   BIN",32,6,w->g.audio_blocks*512u);
 entry(w->sectors+96,"INDEX0  BIN",32,cluster(&w->g,w->g.index[0]),FRESH529_INDEX*512u);
 entry(w->sectors+128,"INDEX1  BIN",32,cluster(&w->g,w->g.index[1]),FRESH529_INDEX*512u);
 entry(w->sectors+160,"WORK    BIN",32,cluster(&w->g,w->g.work_first),FRESH529_WORK*512u);
 entry(w->sectors+192,"IMPORTS    ",16,4,0);entry(w->sectors+224,"EXPORTS    ",16,5,0);
 if(!write_verified(w,DATA+SPC,w->sectors,1))return false;
 for(unsigned c=4;c<=5;c++){memset(w->sectors,0,512);entry(w->sectors,".          ",16,c,0);entry(w->sectors+32,"..         ",16,3,0);if(!write_verified(w,DATA+(c-2)*SPC,w->sectors,1))return false;}
 progress(w,3,0,1);if(!indexes(w)||!io.sync(io.ctx))return false;
 /* Publish valid partition last, after every referenced initial structure
  * has passed exact write/readback. Request stays pending until outer full
  * native admission succeeds and completion is programmed in flash. */
 memset(w->sectors,0,512);uint8_t*p=w->sectors+446;p[1]=p[2]=p[3]=0xff;p[4]=12;p[5]=p[6]=p[7]=0xff;put(p+8,PART);put(p+12,capacity-PART);w->sectors[510]=0x55;w->sectors[511]=0xaa;
 if(!write_verified(w,0,w->sectors,1)||!io.sync(io.ctx))return false;progress(w,4,1,1);return true;
}
