#include "fat.h"
#include <string.h>
extern void eject449_phase(unsigned) __attribute__((weak));
#define EOC 0x0ffffff8u
static uint32_t u32(const uint8_t*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static unsigned u16(const uint8_t*p){return p[0]|(unsigned)p[1]<<8;}
typedef struct {fat_view v;fat422_bitmap bits;uint8_t bitmap[512];uint32_t bit_page;bool dirty;uint8_t a[512],b[512];uint32_t cached;} walk;
static bool flush_impl(walk*w){if(!w->dirty)return true;if(!w->bits.write(w->bits.ctx,w->bit_page,w->bitmap))return false;w->dirty=false;return true;}
static bool flush(walk*w){if(eject449_phase)eject449_phase(18);bool ok=flush_impl(w);if(eject449_phase)eject449_phase(19);return ok;}
static bool bit_test_impl(walk*w,uint32_t n,bool set,bool*out){
 uint32_t page=n/4096;if(page>=w->bits.sectors)return false;
 if(w->bit_page!=page){if(!flush(w)||!w->bits.read(w->bits.ctx,page,w->bitmap))return false;w->bit_page=page;}
 unsigned at=(n%4096)/8,mask=1u<<(n%8);*out=(w->bitmap[at]&mask)!=0;if(set){w->bitmap[at]|=mask;w->dirty=true;}return true;
}
static bool bit_test(walk*w,uint32_t n,bool set,bool*out){if(eject449_phase)eject449_phase(20);bool ok=bit_test_impl(w,n,set,out);if(eject449_phase)eject449_phase(21);return ok;}
static bool valid(walk*w,uint32_t c){return c>=2&&c-2<w->v.clusters;}
static fat_result next(walk*w,uint32_t c,uint32_t*n){
 if(!valid(w,c))return FAT_LAYOUT;uint32_t sector=c/128;
 if(w->cached!=sector){
  if(eject449_phase)eject449_phase(16);bool ok=w->v.read(w->v.ctx,w->v.fat_start+sector,w->a,1)&&w->v.read(w->v.ctx,w->v.fat_start+w->v.fat_sectors+sector,w->b,1);if(eject449_phase)eject449_phase(17);if(!ok)return FAT_IO;
  if(memcmp(w->a,w->b,512))return FAT_LAYOUT;w->cached=sector;
 }*n=u32(w->a+(c%128)*4)&0x0fffffff;return FAT_OK;
}
static fat_result claim_impl(walk*w,uint32_t c,uint32_t *length){
 *length=0;while(1){
  if(!valid(w,c)||(w->v.unavailable_from&&c>=w->v.unavailable_from))return FAT_LAYOUT;uint32_t bit=c-2;
  bool owned;if(!bit_test(w,bit,true,&owned))return FAT_IO;if(owned)return FAT_CROSSLINK;(*length)++;
  fat_result r=next(w,c,&c);if(r)return r;if(c>=EOC)return FAT_OK;
 }
}
static fat_result claim(walk*w,uint32_t c,uint32_t*length){if(eject449_phase)eject449_phase(12);fat_result r=claim_impl(w,c,length);if(eject449_phase)eject449_phase(13);return r;}
typedef struct {uint32_t first,parent,cluster,sector,entry;bool ended,dot,dotdot;} directory;
fat_result fat422_validate(fat_view v,fat422_bitmap bits,fat_visit visit,void*ctx){
 if(!v.read||!bits.read||!bits.write||!bits.sync||!v.clusters||v.clusters>0x0fffffee||bits.sectors<((uint64_t)v.clusters+4095)/4096||!v.sectors_per_cluster||v.sectors_per_cluster>128||(v.sectors_per_cluster&(v.sectors_per_cluster-1))||v.fat_start<1||v.fat_sectors<((uint64_t)v.clusters+2+127)/128||(uint64_t)v.fat_start+2ull*v.fat_sectors!=v.data_start||(uint64_t)v.data_start+(uint64_t)v.clusters*v.sectors_per_cluster!=v.sectors)return FAT_LAYOUT;
 walk w={.v=v,.bits=bits,.cached=UINT32_MAX,.bit_page=UINT32_MAX};
 uint8_t zero[512]={0};for(uint32_t i=0;i<((uint64_t)v.clusters+4095)/4096;i++)if(!bits.write(bits.ctx,i,zero))return FAT_IO;if(!bits.sync(bits.ctx))return FAT_IO;
 uint8_t sector[512];if(!v.read(v.ctx,0,sector,1))return FAT_IO;
 if(sector[510]!=0x55||sector[511]!=0xaa||u16(sector+11)!=512||sector[13]!=v.sectors_per_cluster||u16(sector+14)!=v.fat_start||sector[16]!=2||u16(sector+17)||u16(sector+19)||u16(sector+22)||u32(sector+32)!=v.sectors||u32(sector+36)!=v.fat_sectors||u16(sector+40)||u16(sector+42)||u32(sector+44)!=2)return FAT_LAYOUT;
 uint32_t length;fat_result r=claim(&w,2,&length);if(r)return r;
 directory stack[8]={{.first=2,.cluster=2}};unsigned depth=0;uint64_t budget=0;
 while(1){
  directory*d=stack+depth;
  if(d->sector==v.sectors_per_cluster){uint32_t n;r=next(&w,d->cluster,&n);if(r)return r;
   if(n>=EOC){if(depth&&(!d->dot||!d->dotdot))return FAT_LAYOUT;if(!depth)break;depth--;continue;}
   d->cluster=n;d->sector=d->entry=0;
  }
  if(eject449_phase)eject449_phase(14);bool read_ok=v.read(v.ctx,v.data_start+(d->cluster-2)*v.sectors_per_cluster+d->sector,sector,1);if(eject449_phase)eject449_phase(15);if(!read_ok)return FAT_IO;
  bool descended=false;
  while(d->entry<16){
   if(++budget>(uint64_t)v.sectors*16)return FAT_LIMIT;
   const uint8_t*e=sector+32*d->entry++;
   if(d->ended)break;if(!e[0]){d->ended=true;break;}if(e[0]==0xe5||e[11]==0x0f)continue;
   uint8_t attr=e[11];if(attr&0xc0)return FAT_LAYOUT;
   uint32_t first=((uint32_t)u16(e+20)<<16)|u16(e+26),size=u32(e+28);
   if(attr&8){if(depth||attr!=8||first||size)return FAT_LAYOUT;continue;}
   if(!memcmp(e,".          ",11)){if(!depth||d->dot||!(attr&16)||first!=d->first||size)return FAT_LAYOUT;d->dot=true;continue;}
   if(!memcmp(e,"..         ",11)){uint32_t parent=d->parent==2?0:d->parent;if(!depth||d->dotdot||!(attr&16)||first!=parent||size)return FAT_LAYOUT;d->dotdot=true;continue;}
   if(e[0]=='.')return FAT_LAYOUT;
   fat_file f={.cluster=first,.bytes=size,.parent=d->first,.attributes=attr,.depth=depth};memcpy(f.name,e,11);
   if(attr&16){if(size||!valid(&w,first))return FAT_LAYOUT;r=claim(&w,first,&length);if(r)return r;
    if(visit&&!visit(ctx,&f))return FAT_VISITOR;if(depth+1>=8)return FAT_LIMIT;
    stack[++depth]=(directory){.first=first,.parent=d->first,.cluster=first};descended=true;break;
   }
   if(!size){if(first)return FAT_LAYOUT;}else{
    r=claim(&w,first,&length);if(r)return r;
    uint64_t needed=((uint64_t)size+v.sectors_per_cluster*512-1)/(v.sectors_per_cluster*512);
    if(length!=needed)return FAT_LAYOUT;
   }
   if(visit&&!visit(ctx,&f))return FAT_VISITOR;
  }
  if(!descended&&d->ended){if(depth&&(!d->dot||!d->dotdot))return FAT_LAYOUT;if(!depth)break;depth--;continue;}
  if(!descended&&d->entry==16){d->entry=0;d->sector++;}
 }
 if(eject449_phase)eject449_phase(8);
 // All nonfree clusters must be owned, including hidden/trash files. Check
 // every FAT sector/mirror; cycles, crosslinks and orphan allocations refuse.
 /*505: check the same entries and both mirrors, but resolve FAT/bitmap
  * cache pages once per bounded span. No timing calls or virtual lookups per
  * cluster; the immutable final sweep never changes ownership bits. */
 for(uint32_t c=2;c<v.clusters+2;){uint32_t ignored;
  r=next(&w,c,&ignored);if(r)return r;
  bool owned;if(!bit_test(&w,c-2,false,&owned))return FAT_IO;
  uint32_t stop=(c/128+1)*128,bit_stop=((c-2)/4096+1)*4096+2;
  if(stop>bit_stop)stop=bit_stop;if(stop>v.clusters+2)stop=v.clusters+2;
  for(;c<stop;c++){
   uint32_t n=u32(w.a+(c%128)*4)&0x0fffffffu,bit=c-2;
   owned=(w.bitmap[(bit%4096)/8]&(1u<<(bit%8)))!=0;
   if(v.unavailable_from&&c>=v.unavailable_from){if(owned||n!=0x0ffffff7u)return FAT_LAYOUT;continue;}
   if(!owned&&n)return FAT_ORPHAN;if(owned&&!n)return FAT_LAYOUT;
  }
 }
 if(eject449_phase)eject449_phase(9);
 if(!flush(&w)||!bits.sync(bits.ctx))return FAT_IO;return FAT_OK;
}
