#include "view.h"
#include <string.h>
#include <stdio.h>
#include <limits.h>
#define SPC 64u
#define FIXED 212u
/* node words: parent,first,clusters,bytes,kind,payload,childBase,childCount */
static uint32_t get(const uint8_t*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=n>>(8*i);}
static void p16(uint8_t*p,uint32_t n){p[0]=n;p[1]=n>>8;}
#ifdef IMPORT469
#define IMPORT469_ROM __attribute__((section(".conv469.rodata.readme")))
#else
#define IMPORT469_ROM
#endif
static const char readme[2048] IMPORT469_ROM="SP-1 MUSIC\r\nSONGS / SONG01..16 / TRACK1..4 / TAKES / T0000001.WAV\r\nCURRENT.TXT identifies the loaded take. Songs contain tracks and their saved takes.\r\nCopy WAVs to your computer. In editable drive mode add, replace or delete WAVs in TAKES.\r\nDirect import: WAV (PCM8/16/24/32, float32/64, extensible), PCM AIFF/AIFC, MP3 and native FLAC. Mono/stereo; common8k-96k rates convert to48k. Clips up to about8minutes at normal speed, subject to decoder memory and drive free space. Other formats: use the website. Eject and wait for checking/conversion/saving.\r\nUnsupported lane files reject the whole session; retain computer originals.\r\nEject in Finder to finish importing. Keep USB connected while the track lights show processing. Tape Looper returns automatically when saving is complete. Processing lights: T1 check, T2 plan, T3 import, T4 save. Larger batches take longer. This view is not a settings backup.\r\n";
__attribute__((weak)) void import458_report(uint32_t off,uint8_t*out,uint32_t len){(void)off;memset(out,0,len);}
static unsigned note(char*b,uint32_t ordinal){return ordinal?(unsigned)snprintf(b,96,"Active take: TAKES/T%07u.WAV\r\n",(unsigned)ordinal):(unsigned)snprintf(b,96,"Empty recording lane. Saved takes remain in TAKES.\r\n");}
static bool dir(uint32_t kind){return kind<=5;}
static bool append(cat422_manifest*m,uint32_t*next,uint32_t parent,uint32_t bytes,uint32_t kind,uint32_t payload,uint32_t base,uint32_t count){
 uint64_t size=dir(kind)?((uint64_t)count+(kind?2:0))*32:bytes;
 uint64_t clusters=(size+SPC*512-1)/(SPC*512);if(!clusters)clusters=1;
 if(clusters>0x0ffffff0u-*next)return false;
 uint8_t n[32];put(n,parent);put(n+4,*next);put(n+8,clusters);put(n+12,bytes);put(n+16,kind);put(n+20,payload);put(n+24,base);put(n+28,count);
 if(!cat422_manifest_append(m,n))return false;*next+=(uint32_t)clusters;return true;
}
bool cat422_view_build(cat422_view*s,cat422_manifest*files,cat422_manifest*nodes,pg422_io spool,pool_io raw){
 if(!s)return false;memset(s,0,sizeof *s);
 if(!files||!nodes||files==nodes||!files->ready||files->count>PG422_LIMIT-FIXED-1)return false;
 uint32_t count[65]={0},active[65]={0};uint8_t b[32];unsigned previous=0;
 for(uint32_t i=0;i<files->count;i++){
  if(!cat422_manifest_get(files,i,b))return false;unsigned lane=get(b)&65535;
  if(lane>=65||lane<previous||get(b+4)!=count[lane]+1||(get(b)>>16)>1)return false;previous=lane;count[lane]++;
  if(get(b)>>16){if(active[lane])return false;active[lane]=get(b+4);}
  /* Admit every WAV before publishing any host-visible geometry. */
  if(!cat422_wav_open(&s->wav,files,raw,i))return false;
 }
 if(!cat422_manifest_begin(nodes,spool,files->generation))return false;
 uint32_t next=2;
 if(!append(nodes,&next,0,0,0,0,1,7)||!append(nodes,&next,0,0,1,0,4,16)||!append(nodes,&next,0,sizeof readme-1,6,0,0,0)||!append(nodes,&next,0,0,5,64,FIXED+files->count-count[64],count[64]))goto fail;
 for(unsigned i=0;i<16;i++)if(!append(nodes,&next,1,0,2,i,20+i*4,4))goto fail;
 for(unsigned i=0;i<64;i++)if(!append(nodes,&next,4+i/4,0,3,i,0,2))goto fail;
 uint32_t start=FIXED;for(unsigned i=0;i<64;i++){if(!append(nodes,&next,20+i,0,4,i,start,count[i]))goto fail;start+=count[i];}
 for(unsigned i=0;i<64;i++){char text[96];if(!append(nodes,&next,20+i,note(text,active[i]),7,active[i],0,0))goto fail;}
 for(uint32_t i=0;i<files->count;i++){if(!cat422_manifest_get(files,i,b))goto fail;unsigned lane=get(b)&65535;if(!append(nodes,&next,lane==64?3:84+lane,44+get(b+24)*4,8,i,0,0))goto fail;}
 if(!append(nodes,&next,0,512,9,0,0,0)||!cat422_manifest_finish(nodes))goto fail;
 s->files=files;s->nodes=nodes;s->raw=raw;s->used=next-2;s->clusters=s->used<65536?65536:s->used;s->fat=((uint64_t)(s->clusters+2)*4+511)/512;s->data=32+2*s->fat;
 uint64_t sectors=s->data+(uint64_t)s->clusters*SPC;if(sectors>UINT32_MAX)goto fail;s->sectors=sectors;s->wav.ready=false;s->ready=true;return true;
fail:nodes->building=false;s->ready=false;return false;
}
bool cat422_view_open(cat422_view*s,cat422_manifest*files,cat422_manifest*nodes,pool_io raw){
 if(!s)return false;memset(s,0,sizeof *s);
 if(!files||!nodes||!files->ready||!nodes->ready||files->generation!=nodes->generation||files->count>PG422_LIMIT-FIXED-1||nodes->count!=files->count+FIXED+1)return false;
 uint32_t next=2;uint8_t b[32];
 /* Reopen verifies the whole compact node spool before exposing a drive;
  * it never silently regenerates a baseline under staged host edits. */
 for(uint32_t i=0;i<nodes->count;i++){
  if(!cat422_manifest_get(nodes,i,b)||get(b+4)!=next||!get(b+8)||get(b+8)>0x0ffffff0u-next||get(b)>=nodes->count||get(b+16)>9)return false;
  if(i>=FIXED&&i<FIXED+files->count&&(get(b+16)!=8||get(b+20)!=i-FIXED))return false;
  next+=get(b+8);
 }
 s->files=files;s->nodes=nodes;s->raw=raw;s->used=next-2;s->clusters=s->used<65536?65536:s->used;s->fat=((uint64_t)(s->clusters+2)*4+511)/512;s->data=32+2*s->fat;
 uint64_t sectors=s->data+(uint64_t)s->clusters*SPC;if(sectors>UINT32_MAX)return false;s->sectors=sectors;s->ready=true;return true;
}
static bool owner(cat422_view*s,uint32_t cluster,uint8_t b[32],bool*found){
 /*507: the immutable generated nodes fill only this original prefix. The
  * overlay is checked first by drive422_read, so host-created data is never
  * hidden by returning the correct zero baseline outside that prefix. */
 *found=false;if(cluster<2||cluster-2>=s->used)return true;
 /* Immutable baseline nodes for this exclusive drive session. Host edits
  * are served by the overlay, never by mutating this baseline cache. */
 if(s->owner_valid){uint32_t first=get(s->owner_node+4),n=get(s->owner_node+8);if(cluster>=first&&cluster-first<n){memcpy(b,s->owner_node,32);*found=true;return true;}}
 uint32_t lo=0,hi=s->nodes->count;*found=false;
 while(lo<hi){uint32_t mid=lo+(hi-lo)/2;if(!cat422_manifest_get(s->nodes,mid,b))return false;uint32_t first=get(b+4),n=get(b+8);if(cluster<first)hi=mid;else if(cluster-first>=n)lo=mid+1;else{memcpy(s->owner_node,b,32);s->owner_valid=true;*found=true;return true;}}
 return true;
}
static bool entry(cat422_view*s,uint32_t id,uint8_t*e){
 uint8_t b[32];if(!cat422_manifest_get(s->nodes,id,b))return false;uint32_t k=get(b+16),p=get(b+20);char name[12];
 if(k==1)memcpy(name,"SONGS      ",11);else if(k==2)snprintf(name,12,"SONG%02u     ",(unsigned)p+1);else if(k==3)snprintf(name,12,"TRACK%u     ",(unsigned)p%4+1);else if(k==4)memcpy(name,"TAKES      ",11);else if(k==5)memcpy(name,"UNASSIGN   ",11);else if(k==6)memcpy(name,"README  TXT",11);else if(k==7)memcpy(name,"CURRENT TXT",11);else if(k==9)memcpy(name,"RESULT  TXT",11);else if(k==8){uint8_t f[32];if(!cat422_manifest_get(s->files,p,f))return false;snprintf(name,12,"T%07uWAV",(unsigned)get(f+4));}else return false;
 memcpy(e,name,11);e[11]=dir(k)?16:(k==8&&s->writable?32:33);
 /* Keep session diagnostics and an empty migration-only lane out of Finder.
  * Preserve fixed node IDs and expose any real orphaned recordings. */
 if(k==9||(k==5&&!get(b+28)))e[11]|=2;
 p16(e+20,get(b+4)>>16);p16(e+26,get(b+4));put(e+28,dir(k)?0:get(b+12));p16(e+16,0x5c21);p16(e+24,0x5c21);return true;
}
static void no_index_entry(uint8_t*e,unsigned part){
 static const char name[]=".metadata_never_index";
 static const char shortname[]="METADA~1   ";
 if(part==2){memcpy(e,shortname,11);e[11]=3;return;}
 unsigned sum=0;for(unsigned i=0;i<11;i++)sum=((sum&1)?128:0)+(sum>>1)+(unsigned char)shortname[i],sum&=255;
 static const uint8_t offsets[13]={1,3,5,7,9,14,16,18,20,22,24,28,30};
 unsigned seq=part?1:2;e[0]=seq|(part?0:0x40);e[11]=15;e[13]=sum;
 for(unsigned i=0;i<13;i++){unsigned n=(seq-1)*13+i;p16(e+offsets[i],n<sizeof(name)-1?(unsigned char)name[n]:n==sizeof(name)-1?0:0xffff);}
}
static bool content(cat422_view*s,const uint8_t*n,uint32_t off,uint8_t*out,uint32_t len){
 uint32_t k=get(n+16),p=get(n+20);if(k==9){import458_report(off,out,len);return true;}if(k==6){memcpy(out,readme+off,len);if(off+len>1024){uint32_t start=off<1024?1024:off;import458_report(start-1024,out+start-off,off+len-start);}return true;}if(k==7){char b[96];note(b,p);memcpy(out,b+off,len);return true;}
 if(k!=8)return false;if(!s->wav.ready||s->wav.file!=p)if(!cat422_wav_open(&s->wav,s->files,s->raw,p))return false;return cat422_wav_read(&s->wav,off,out,len);
}
bool cat422_view_read(cat422_view*s,uint32_t lba,uint8_t*b,uint32_t count){
 if(!s||!s->ready||!b||!count||lba>=s->sectors||count>s->sectors-lba||!s->files->ready||!s->nodes->ready||s->files->generation!=s->nodes->generation)return false;
 for(;count;count--,lba++,b+=512){memset(b,0,512);
  if(lba==0||lba==6){b[0]=0xeb;b[1]=0x58;b[2]=0x90;memcpy(b+3,"MSWIN4.1",8);p16(b+11,512);b[13]=SPC;p16(b+14,32);b[16]=2;b[21]=0xf8;p16(b+24,63);p16(b+26,255);put(b+32,s->sectors);put(b+36,s->fat);put(b+44,2);p16(b+48,1);p16(b+50,6);b[64]=0x80;b[66]=0x29;put(b+67,s->files->generation);memcpy(b+71,"SP1 MUSIC  ",11);memcpy(b+82,"FAT32   ",8);b[510]=0x55;b[511]=0xaa;continue;}
  if(lba==1||lba==7){put(b,0x41615252);put(b+484,0x61417272);put(b+488,s->unavailable_from?s->unavailable_from-2-s->used:s->clusters-s->used);put(b+492,s->used==s->clusters?UINT32_MAX:s->used+2);put(b+508,0xaa550000);continue;}
  if(lba<32)continue;
  if(lba<s->data){uint32_t page=(lba-32)%s->fat;bool hit=false;
   /* Immutable virtual baseline only. Overlay lookup remains outside this
    * function and independently checks each host-modified FAT copy. */
   for(unsigned slot=0;slot<4;slot++)if((s->fat_cache_valid&(1u<<slot))&&s->fat_cache_page[slot]==page){memcpy(b,s->fat_cache[slot],512);hit=true;break;}if(hit)continue;
   uint32_t start=page*128;uint8_t n[32];bool found=false;uint32_t end=0;
   for(unsigned i=0;i<128;i++){uint32_t c=start+i,v=0;if(c<2)v=c?0x0fffffffu:0x0ffffff8u;else if(c>=s->used+2){/* Validated node extents are contiguous; no lookup beyond them. */ if(s->unavailable_from&&c>=s->unavailable_from&&c<s->clusters+2)v=0x0ffffff7u;}else{if(c>=end){if(!owner(s,c,n,&found))return false;end=found?get(n+4)+get(n+8):UINT32_MAX;}if(found)v=c+1<end?c+1:0x0fffffffu;else if(s->unavailable_from&&c>=s->unavailable_from&&c<s->clusters+2)v=0x0ffffff7u;}put(b+i*4,v);}/* The fixed directory tree starts in the first two FAT pages. Retain
    * these during long audio-chain scans; other pages use two streaming slots. */
   unsigned slot=page<2?page:2+s->fat_cache_next++%2;memcpy(s->fat_cache[slot],b,512);s->fat_cache_page[slot]=page;s->fat_cache_valid|=1u<<slot;continue;}
  uint32_t cluster=2+(lba-s->data)/SPC;uint8_t n[32];bool found;if(!owner(s,cluster,n,&found))return false;if(!found)continue;
  uint32_t off=((cluster-get(n+4))*SPC+(lba-s->data)%SPC)*512,k=get(n+16);
  if(dir(k)){for(unsigned slot=0;slot<16;slot++){uint32_t ordinal=off/32+slot;uint8_t*e=b+slot*32;
   if(k&&ordinal<2){memcpy(e,ordinal?"..         ":".          ",11);e[11]=16;uint32_t cl=get(n+4);if(ordinal){uint32_t parent=get(n);uint8_t p[32];if(!cat422_manifest_get(s->nodes,parent,p))return false;cl=parent?get(p+4):0;}p16(e+20,cl>>16);p16(e+26,cl);continue;}
   if(k)ordinal-=2;if(ordinal>=get(n+28))continue;if(!k&&ordinal>=4){no_index_entry(e,ordinal-4);continue;}if(!k&&ordinal==3){if(!entry(s,s->nodes->count-1,e))return false;continue;}uint32_t child=k==3?(ordinal?148:84)+get(n+20):get(n+24)+ordinal;if(!entry(s,child,e))return false;
  }}else if(off<get(n+12)){uint32_t len=get(n+12)-off;if(len>512)len=512;if(!content(s,n,off,b,len))return false;}
 }return true;
}
