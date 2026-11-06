/* Maintenance-only FAT32 ownership policy. No dependency on the audio engine.
 * Existing allocations are immutable except /TAPELOOP/{IMPORTS,EXPORTS}.
 * New allocations may use only clusters that were free at admission.
 * The complete graph is checked before exposing media: crosslinks, cycles,
 * mirrors, orphan allocations and unsupported geometries refuse admission.
 */
#ifndef SP1_SHARED_DRIVE_H
#define SP1_SHARED_DRIVE_H
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#define DRIVE_CLUSTERS 524288u
#define DRIVE_EOC 0x0ffffff8u
typedef bool (*drive_read_fn)(uint32_t, uint8_t*, uint32_t);
typedef struct {
 uint32_t first,blocks,reserved,fat,data,spc,end,root,own;
 uint8_t seen[DRIVE_CLUSTERS/8],protected[DRIVE_CLUSTERS/8];
 uint32_t directory[256],directory_count,dir_cached,fat_cached;
 uint8_t directory_kind[256];
 uint8_t sector[512],mirror[512];
 drive_read_fn read;
 unsigned entries,dirs; bool native,imports,exports,legacy;
 uint32_t native_start[4],native_bytes[4];uint8_t native_mask;
} shared_drive;
static uint32_t sd32(const uint8_t*b){return b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;}
static uint32_t sd16(const uint8_t*b){return b[0]|(uint32_t)b[1]<<8;}
static bool sd_bit(const uint8_t*b,uint32_t c){return (b[c/8]>>(c%8))&1;}
static void sd_set(uint8_t*b,uint32_t c){b[c/8]|=1u<<(c%8);}
static bool sd_valid(shared_drive*g,uint32_t c){return c>=2&&c<g->end;}
static uint32_t sd_lba(shared_drive*g,uint32_t c){return g->data+(c-2)*g->spc;}
static bool sd_next(shared_drive*g,uint32_t c,uint32_t*n){if(!sd_valid(g,c))return false;uint32_t f=g->first+g->reserved+c/128;if(g->fat_cached!=f){if(!g->read(f,g->sector,1))return false;g->fat_cached=f;}*n=sd32(g->sector+(c%128)*4)&0xfffffff;return *n>=DRIVE_EOC||sd_valid(g,*n);}
/* Exact, checksum-validated root OS names; never trust an 8.3 prefix. */
typedef struct { char text[27]; uint8_t next,sum; bool valid; } sd_lfn;
static uint8_t sd_sum(const uint8_t*e){uint8_t s=0;for(unsigned i=0;i<11;i++)s=((s&1)?128:0)+(s>>1)+e[i];return s;}
static void sd_lfn_add(sd_lfn*l,const uint8_t*e){
 unsigned n=e[0]&31; if(e[0]&64){memset(l,0,sizeof(*l));l->valid=n>=1&&n<=2;l->next=n;l->sum=e[13];}
 if(!l->valid||n!=l->next||e[13]!=l->sum||e[12]||sd16(e+26)||(e[0]&0xa0)){l->valid=false;return;}
 const unsigned pos[]={1,3,5,7,9,14,16,18,20,22,24,28,30};
 for(unsigned i=0;i<13;i++){unsigned v=sd16(e+pos[i]);if(v>127&&v!=65535){l->valid=false;return;}l->text[(n-1)*13+i]=(v==65535)?0:(char)v;}l->next--;
}
static bool sd_os(sd_lfn*l,const uint8_t*e){return l->valid&&!l->next&&sd_sum(e)==l->sum&&(e[11]&16)&&
 (!strcmp(l->text,".Spotlight-V100")||!strcmp(l->text,".fseventsd")||!strcmp(l->text,".Trashes")||!strcmp(l->text,".TemporaryItems"));}
/* Special directory entries stay fixed; free slots can hold new host files.
 * Existing protected file names, allocation, length and attributes are fixed.
 * Only standard FAT timestamp fields may change during host access. */
static bool sd_directory_write(shared_drive*g,const uint8_t*before,const uint8_t*after,uint32_t base){
 for(unsigned o=0;o<512;o+=32){const uint8_t*a=before+o,*b=after+o;
  if(!memcmp(a,b,32))continue;
  if(sd_bit(g->seen,base+o/32)){if(a[11]==15)return false;for(unsigned i=0;i<32;i++)if(a[i]!=b[i]&&!(i>=13&&i<=19)&&!(i>=22&&i<=25))return false;}
  else if(b[0]&&b[0]!=229&&b[11]!=15){uint32_t c=sd16(b+26)+(sd16(b+20)<<16);if(c&&(!sd_valid(g,c)||sd_bit(g->protected,c)))return false;if(b[11]&8)return false;}
 }
 return true;
}
/* Read one entry at a time: recursion keeps only a 32-byte entry, never a sector
 * or FAT pointer that a nested lookup can invalidate. */
static bool sd_walk(shared_drive*g,uint32_t start,bool dir,bool mutable,unsigned depth,unsigned kind,uint32_t bytes){
 if(depth>16||!sd_valid(g,start)||++g->entries>g->blocks*16ull||(dir&&++g->dirs>1024))return false;
 uint32_t c=start,count=0;bool ended=false;sd_lfn lfn={0};
 do{
  if(!sd_valid(g,c)||sd_bit(g->seen,c))return false;
  sd_set(g->seen,c);if(!mutable)sd_set(g->protected,c);if(dir&&(kind==1||kind==2)){if(g->directory_count>=256)return false;g->directory_kind[g->directory_count]=kind;g->directory[g->directory_count++]=c;}
  count++;
  if(dir){for(uint32_t sec=0;sec<g->spc;sec++)for(unsigned off=0;off<512;off+=32){
   if(ended)break;
   uint32_t dl=sd_lba(g,c)+sec;if(g->dir_cached!=dl){if(!g->read(dl,g->mirror,1))return false;g->dir_cached=dl;}
   uint8_t e[32];memcpy(e,g->mirror+off,32);
   if(!e[0]){ended=true;break;}if(e[0]==229){lfn.valid=false;continue;}if(e[11]==15){sd_lfn_add(&lfn,e);continue;}bool os=kind==1&&sd_os(&lfn,e);lfn.valid=false;if(e[11]&8||!memcmp(e,".          ",11)||!memcmp(e,"..         ",11))continue;
   if(e[11]&0xc0)return false;
   uint32_t cl=sd16(e+26)+(sd16(e+20)<<16),len=sd32(e+28);bool isdir=e[11]&16,childmutable=mutable;unsigned childkind=0;if(os)childmutable=true;
   if(kind==1&&(!memcmp(e,"TAPELOOP   ",11)||!memcmp(e,"SP1        ",11))){if(g->native||!isdir)return false;g->native=true;g->own=cl;childkind=2;}
   if((kind==2||(kind==1&&g->legacy))&&(!memcmp(e,"IMPORTS    ",11)||!memcmp(e,"EXPORTS    ",11))){if(!isdir)return false;childmutable=true;if(e[0]=='I'){if(g->imports)return false;g->imports=true;}else{if(g->exports)return false;g->exports=true;}}
   if(kind==2){const char*names[]={"INDEX0  BIN","INDEX1  BIN","AUDIO   BIN","WORK    BIN"};for(unsigned k=0;k<4;k++)if(!memcmp(e,names[k],11)){if(isdir||(g->native_mask&(1u<<k)))return false;g->native_mask|=1u<<k;g->native_start[k]=cl;g->native_bytes[k]=len;}}
   if(isdir){if(len||!sd_walk(g,cl,true,childmutable,depth+1,childkind,0))return false;}
   else if(len){if(!sd_walk(g,cl,false,childmutable,depth+1,0,len))return false;}
   else if(cl)return false;
  }}
  uint32_t next;if(!sd_next(g,c,&next))return false;if(next>=DRIVE_EOC)break;c=next;
 }while(true);
 return dir||count==(bytes+(uint64_t)g->spc*512-1)/(g->spc*512);
}
static bool sd_admit(shared_drive*g,drive_read_fn read,uint32_t capacity){
 memset(g,0,sizeof(*g));g->read=read;
 if(!read(0,g->sector,1)||sd16(g->sector+510)!=0xaa55)return false;
 g->legacy=!memcmp(g->sector,"SP1TAIL3",8);
 unsigned parts=0;for(unsigned p=446;p<510;p+=16)if(g->sector[p+4]){parts++;if(g->sector[p+4]!=11&&g->sector[p+4]!=12)return false;g->first=sd32(g->sector+p+8);g->blocks=sd32(g->sector+p+12);}
 if(parts!=1||!g->first||!g->blocks||g->first>=capacity||g->blocks>capacity-g->first||!read(g->first,g->sector,1))return false;
 uint8_t*b=g->sector;g->spc=b[13];g->reserved=sd16(b+14);g->fat=sd32(b+36);g->root=sd32(b+44);
 if(sd16(b+510)!=0xaa55||sd16(b+11)!=512||g->spc<16||g->spc>128||(g->spc&(g->spc-1))||g->reserved<8||b[16]!=2||sd16(b+17)||sd16(b+19)||sd16(b+22)||sd16(b+40)||sd16(b+42)||sd32(b+32)!=g->blocks||sd32(b+28)!=g->first||sd16(b+48)!=1||sd16(b+50)!=6||!g->fat||g->fat>4096||g->reserved+2*g->fat>=g->blocks)return false;
 g->data=g->first+g->reserved+2*g->fat;g->end=2+(g->first+g->blocks-g->data)/g->spc;
 if(g->end<65527||g->end>DRIVE_CLUSTERS||g->end*4>g->fat*512||!sd_valid(g,g->root)||!read(g->first+6,g->mirror,1)||memcmp(b,g->mirror,512))return false;
 for(unsigned i=0;i<2;i++){if(!read(g->first+(i?7:1),b,1)||sd32(b)!=0x41615252||sd32(b+484)!=0x61417272||sd32(b+508)!=0xaa550000)return false;}
 for(uint32_t s=0;s<g->fat;s++){if(!read(g->first+g->reserved+s,b,1)||!read(g->first+g->reserved+g->fat+s,g->mirror,1)||memcmp(b,g->mirror,512))return false;if(!s&&((sd32(b)&0xfffffff)<DRIVE_EOC||(sd32(b+4)&0xfffffff)<DRIVE_EOC))return false;}
 if(!sd_walk(g,g->root,true,false,0,1,0)||!g->native||!g->imports||!g->exports)return false;
 for(uint32_t s=0;s<g->fat;s++){if(!read(g->first+g->reserved+s,b,1))return false;for(unsigned i=0;i<128;i++){uint32_t c=s*128+i;if(c<2||c>=g->end)continue;uint32_t n=sd32(b+i*4)&0xfffffff;if(n==0xffffff7){sd_set(g->protected,c);continue;}if((n!=0)!=sd_bit(g->seen,c))return false;}}
 /* Graph validation is complete. Reuse the visitation bitmap for immutable
  * directory slots, so host-created root files can subsequently grow/rename.
  * Existing slots, including zero-length foreign files and LFNs, stay fixed. */
 memset(g->seen,0,sizeof(g->seen));
 sd_lfn root_lfn={0};uint32_t lfn_slots[2]={0};unsigned lfn_count=0;
 for(unsigned pass=0;pass<2;pass++)for(unsigned i=0;i<g->directory_count;i++)for(uint32_t sec=0;sec<g->spc;sec++){
  if((g->directory_kind[i]==1)!=(pass==0))continue;
  if(!read(sd_lba(g,g->directory[i])+sec,b,1))return false;
  for(unsigned off=0;off<512;off+=32){uint8_t*e=b+off;uint32_t slot=(i*g->spc+sec)*16+off/32;
   if(!e[0]||e[0]==229){root_lfn.valid=false;lfn_count=0;continue;}sd_set(g->seen,slot);
   if(e[11]==15){if(e[0]&64)lfn_count=0;sd_lfn_add(&root_lfn,e);if(lfn_count<2)lfn_slots[lfn_count++]=slot;continue;}
   if(g->directory_kind[i]==1&&sd_os(&root_lfn,e)){g->seen[slot/8]&=~(1u<<(slot%8));for(unsigned k=0;k<lfn_count;k++)g->seen[lfn_slots[k]/8]&=~(1u<<(lfn_slots[k]%8));}
   root_lfn.valid=false;lfn_count=0;
  }
 }
 g->fat_cached=0;g->dir_cached=0;return true;
}
static bool sd_write(shared_drive*g,uint32_t lba,const uint8_t*before,const uint8_t*after){
 if(lba<g->first||lba-g->first>=g->blocks)return false;
 if(!memcmp(before,after,512))return true;
 if(lba<g->first+g->reserved){if(lba!=g->first+1&&lba!=g->first+7)return false;return !memcmp(before,after,488)&&!memcmp(before+496,after+496,16);}
 if(lba<g->data){uint32_t s=(lba-g->first-g->reserved)%g->fat;for(unsigned i=0;i<128;i++){
  uint32_t c=s*128+i,a=sd32(before+i*4),b=sd32(after+i*4);if(a==b)continue;
  if(c==1&&((a^b)&~0x08000000u)==0)continue;
  if(!sd_valid(g,c)||sd_bit(g->protected,c)||(b&0xf0000000))return false;
  if(b&&b<DRIVE_EOC&&(!sd_valid(g,b)||sd_bit(g->protected,b)))return false;
 }return true;}
 uint32_t c=2+(lba-g->data)/g->spc;if(!sd_valid(g,c))return false;
 for(unsigned i=0;i<g->directory_count;i++)if(g->directory[i]==c)return sd_directory_write(g,before,after,(i*g->spc+(lba-g->data)%g->spc)*16);
 return !sd_bit(g->protected,c);
}
static bool sd_direct(shared_drive*g,uint32_t lba){if(lba<g->data)return false;uint32_t c=2+(lba-g->data)/g->spc;return sd_valid(g,c)&&!sd_bit(g->protected,c);}
#endif
