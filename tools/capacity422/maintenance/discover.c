#include "discover.h"
#include "../../native593/native.h"
#include <string.h>
#include <stdio.h>
extern void eject449_phase(unsigned);
#define EOC 0x0ffffff8u
static uint32_t get(const uint8_t*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=n>>(8*i);}
/* Only FAT walker directory reads use this cache. Frozen host ownership and
 * memset at build entry bound its lifetime; FAT mirrors remain independently read.
 * Workspace storage avoids adding 4 KiB to the maintenance call stack. */
static bool rd(void*c,uint32_t at,void*b,uint32_t n){
 discover422*s=c;
 if(n!=1||at<s->drive->view.data)return drive422_read(s->drive,at,b,n);
 for(unsigned i=0;i<8;i++)if((s->directory_valid&(1u<<i))&&s->directory_lba[i]==at){memcpy(b,s->directory_cache[i],512);s->directory_hits++;return true;}
 s->directory_misses++;if(!drive422_read(s->drive,at,b,n))return false;
 unsigned slot=s->directory_next++%8;memcpy(s->directory_cache[slot],b,512);s->directory_lba[slot]=at;s->directory_valid|=1u<<slot;return true;
}
/*508: bounded immutable-source read-ahead into the existing eight-sector
 * directory cache. Never crosses a cluster or the final file sector; all reads
 * retain overlay checkpoint and payload verification. FAT cache stays separate. */
static bool source_sector(discover422*s,uint32_t l,uint32_t count,bool readahead){
 if(!readahead)return rd(s,l,s->sector,1);
 for(unsigned i=0;i<8;i++)if((s->directory_valid&(1u<<i))&&s->directory_lba[i]==l)return rd(s,l,s->sector,1);
 if(count>8)count=8;if(!count)return false;
 s->directory_valid&=~255u;if(!drive422_read(s->drive,l,s->directory_cache,count))return false;
 for(unsigned i=0;i<count;i++)s->directory_lba[i]=l+i;
 s->directory_valid=(s->directory_valid&~255u)|((1u<<count)-1);s->directory_next=count;s->directory_misses+=count;
 memcpy(s->sector,s->directory_cache[0],512);return true;
}
static bool seenbit(discover422*s,uint32_t id,bool set,bool*out){if(id>=PG422_LIMIT)return false;uint32_t page=id/4096,at=(id%4096)/8;if(page>=56||!s->seen.read(s->seen.ctx,page,s->sector))return false;unsigned mask=1u<<(id%8);*out=(s->sector[at]&mask)!=0;if(set){s->sector[at]|=mask;if(!s->seen.write(s->seen.ctx,page,s->sector))return false;}return true;}
bool discover422_seen(discover422*s,uint32_t id,bool*out){return s&&s->ready&&out&&seenbit(s,id,false,out);}
static bool chain(discover422*s,uint32_t c,uint32_t*next){cat422_view*v=&s->drive->view;if(c<2||c-2>=v->clusters)return false;
 /* Discovery runs only after host ownership ends and the overlay freezes.
  * Keep FAT data separate from shared scratch, retaining every chain check. */
 uint32_t page=32+c/128;
 if(!s->chain_valid||s->chain_page!=page){s->chain_valid=false;if(!drive422_read(s->drive,page,s->chain_sector,1))return false;s->chain_page=page;s->chain_valid=true;}
 *next=get(s->chain_sector+(c%128)*4)&0xfffffff;return *next>=EOC||(*next>=2&&*next-2<v->clusters);}
typedef discover422_source source;
#ifdef IMPORT469
__attribute__((section(".conv469.text.source508")))
#endif
static bool file_read(void*c,uint32_t at,void*out,uint32_t n){source*f=c;discover422*s=f->s;cat422_view*v=&s->drive->view;if(at>f->bytes||n>f->bytes-at)return false;uint8_t*p=out;uint32_t cl=f->cluster,target=at/32768,skip=target;
 if(f->cached&&f->cached_index<=target){cl=f->cached_cluster;skip-=f->cached_index;}
 while(skip--){if(!chain(s,cl,&cl)||cl>=EOC)return false;}
 f->cached=true;f->cached_cluster=cl;f->cached_index=target;
 while(n){uint32_t offset=at%32768,off=offset%512;
 /*513: long source reads can land directly in the caller's PCM/decoder
  * buffer. Bound by file bytes, cluster and the checked16-sector drive API. */
 /*602: also admit the native envelope's first15-sector payload read. This
  * exact request keeps later reads aligned; general decoder thresholds stay
  * unchanged, as do cluster/file bounds and every overlay integrity check. */
 if(!off&&(n>=8192||(at==512&&n==15u*512u))){uint32_t blocks=n/512,remain=(32768-offset)/512;if(blocks>16)blocks=16;if(blocks>remain)blocks=remain;
  if(cl<2||cl-2>=v->clusters||!drive422_read(s->drive,v->data+(cl-2)*64+offset/512,p,blocks))return false;
  uint32_t bytes=blocks*512;at+=bytes;p+=bytes;n-=bytes;
  if(n&&at%32768==0&&(!chain(s,cl,&cl)||cl>=EOC))return false;continue;
 }
 uint32_t take=512-off;if(take>n)take=n;if(cl<2||cl-2>=v->clusters||!source_sector(s,v->data+(cl-2)*64+offset/512,((f->bytes-at+off+511ull)/512<64-offset/512?(f->bytes-at+off+511ull)/512:64-offset/512),n>=512&&n<8192))return false;memcpy(p,s->sector+off,take);p+=take;at+=take;n-=take;if(n&&at%32768==0&&(!chain(s,cl,&cl)||cl>=EOC))return false;}return true;}
static unsigned role(discover422*s,uint32_t cluster,unsigned*lane){for(unsigned i=0;i<s->nd;i++)if(s->dirs[i].cluster==cluster){*lane=s->dirs[i].lane;return s->dirs[i].role;}*lane=64;return 0;}
static bool original(discover422*s,unsigned lane,const char name[11],uint32_t*index){
 *index=0;if(name[0]!='T'||(memcmp(name+8,"WAV",3)&&memcmp(name+8,"SPN",3)))return true;uint32_t ordinal=0;for(unsigned i=1;i<8;i++){if(name[i]<'0'||name[i]>'9')return true;ordinal=ordinal*10+name[i]-'0';}if(!ordinal)return true;
 uint32_t lo=0,hi=s->drive->files.count;uint8_t b[32];while(lo<hi){uint32_t mid=lo+(hi-lo)/2;if(!cat422_manifest_get(&s->drive->files,mid,b))return false;unsigned l=get(b)&65535;uint32_t rank=get(b+4);if(l<lane||(l==lane&&rank<ordinal))lo=mid+1;else hi=mid;}
 if(lo<s->drive->files.count){if(!cat422_manifest_get(&s->drive->files,lo,b))return false;if((get(b)&65535)==lane&&get(b+4)==ordinal)*index=lo+1;}return true;
}
static bool node_owner(discover422*s,uint32_t cluster,uint8_t b[32],bool*found){
 uint32_t lo=0,hi=s->drive->nodes.count;*found=false;
 /*506: original generated extents occupy one validated contiguous prefix.
  * Newly allocated host clusters beyond it cannot dirty an original file. */
 if(cluster<2||cluster-2>=s->drive->view.used)return true;
 while(lo<hi){uint32_t mid=lo+(hi-lo)/2;if(!cat422_manifest_get(&s->drive->nodes,mid,b))return false;uint32_t first=get(b+4),n=get(b+8);if(cluster<first)hi=mid;else if(cluster-first>=n)lo=mid+1;else{*found=true;return true;}}return true;
}
static bool dirtybit(discover422*s,uint32_t id,bool set,bool*out){fat422_bitmap io=arena422_bitmap_io(&s->drive->arena);uint32_t page=id/4096,at=(id%4096)/8;if(id>=PG422_LIMIT||!io.read(io.ctx,page,s->sector))return false;unsigned mask=1u<<(id%8);*out=(s->sector[at]&mask)!=0;if(set){s->sector[at]|=mask;if(!io.write(io.ctx,page,s->sector))return false;}return true;}
static bool dirty_visit(void*c,uint32_t lba){discover422*s=c;if(lba<s->drive->view.data)return true;uint32_t cluster=2+(lba-s->drive->view.data)/64;uint8_t b[32];bool found;if(!node_owner(s,cluster,b,&found))return false;if(found&&get(b+16)==8){bool prior;return dirtybit(s,get(b+20),true,&prior);}return true;}
static bool input_reject(discover422*s,const fat_file*f,unsigned code){s->input_error=code;memcpy(s->input_name,f->name,11);s->input_name[11]=0;return false;}
static bool visit_impl(void*c,const fat_file*f){
 discover422*s=c;unsigned lane,parent=role(s,f->parent,&lane),r=0;
 if(f->attributes&16){
  if(s->nd>=256)return false;
  if(f->parent==2&&!memcmp(f->name,"SONGS      ",11))r=1;
  else if(f->parent==2&&!memcmp(f->name,"UNASSIGN   ",11)){r=4;lane=64;}
  else if(parent==1){for(unsigned i=0;i<16;i++){char name[12];snprintf(name,12,"SONG%02u     ",i+1);if(!memcmp(name,f->name,11)){r=2;lane=i*4;break;}}}
  else if(parent==2){for(unsigned i=0;i<4;i++){char name[12];snprintf(name,12,"TRACK%u     ",i+1);if(!memcmp(name,f->name,11)){r=3;lane+=i;break;}}}
  else if(parent==3&&!memcmp(f->name,"TAKES      ",11))r=4;
  if(parent==4)return input_reject(s,f,3); /* no silent import from unsupported subfolders */
  for(unsigned i=0;i<s->nd;i++)if(r&&s->dirs[i].role==r&&s->dirs[i].lane==lane)return false;
  s->dirs[s->nd++]=(discover422_dir){f->cluster,r,lane};return true;
 }
 if(parent!=4)return true;
 source src={s,f->cluster,f->bytes};wav_source wave={&src,file_read,f->bytes};
 /* Only structurally valid AppleDouble is housekeeping. An unfamiliar audio
  * extension cannot silently turn a failed replacement into a deletion. */
 if(wav_appledouble(wave))return true;
 if(memcmp(f->name+8,"WAV",3)&&memcmp(f->name+8,"AIF",3)&&memcmp(f->name+8,"AFC",3)&&memcmp(f->name+8,"MP3",3)&&memcmp(f->name+8,"FLA",3)&&memcmp(f->name+8,"BWF",3)&&memcmp(f->name+8,"SPN",3))return input_reject(s,f,1);
 if(!f->bytes)return input_reject(s,f,2);
 uint32_t base;if(!original(s,lane,f->name,&base))return false;

 if(base){bool claimed;if(!seenbit(s,base-1,true,&claimed)||claimed)return false;}
 uint32_t hash=pool_crc(UINT32_MAX,f->name,11)^lane,bucket=hash%1024,page=56+bucket/128,offset=(bucket%128)*4;
 if(!s->seen.read(s->seen.ctx,page,s->sector))return false;uint32_t previous=get(s->sector+offset),id=previous;
 for(unsigned n=0;id;n++){uint8_t b[32];if(n>=s->files.count||!cat422_manifest_peek(&s->files,id-1,b))return false;if((get(b)&255)==lane&&!memcmp(b+16,f->name,11))return false;id=get(b+28);}
 uint8_t b[32]={0};put(b,lane|((uint32_t)f->attributes<<8));put(b+4,base);put(b+8,f->cluster);put(b+12,f->bytes);memcpy(b+16,f->name,11);put(b+28,previous);
 if(!cat422_manifest_append(&s->files,b)||!s->seen.read(s->seen.ctx,page,s->sector))return false;put(s->sector+offset,s->files.count);return s->seen.write(s->seen.ctx,page,s->sector);
}
static bool visit(void*c,const fat_file*f){eject449_phase(10);bool ok=visit_impl(c,f);eject449_phase(11);return ok;}
#ifdef IMPORT469
#define OFFLINE472 __attribute__((section(".conv469.text.discover472")))
#else
#define OFFLINE472
#endif
bool OFFLINE472 discover512_build(discover422*s,drive422*d,uint32_t*changes,uint32_t*reservation){
 if(reservation)*reservation=0;uint64_t reserve=0;
 if(changes)*changes=0;
 if(!s||!d||!d->ready||d->arena.state!=ARENA422_COMMITTING)return false;memset(s,0,sizeof *s);s->drive=d;s->seen=arena422_manifest_io(&d->arena,3);memset(s->sector,0,512);for(unsigned i=0;i<64;i++)if(!s->seen.write(s->seen.ctx,i,s->sector))return false;
 if(!cat422_manifest_begin(&s->files,arena422_manifest_io(&d->arena,2),d->arena.generation))return false;
 eject449_phase(2);cat422_view*v=&d->view;fat_view f={s,rd,v->sectors,32,v->fat,v->data,v->clusters,v->unavailable_from,64};if(fat422_validate(f,arena422_bitmap_io(&d->arena),visit,s)!=FAT_OK)return false;
 eject449_phase(3);
 /* All exact filenames are claimed before allocation-based rename matches.
  * Safe-save backups cannot steal the identity of the replacement file. */
 for(uint32_t i=0;i<s->files.count;i++){uint8_t row[32],node[32],desc[32];if(!cat422_manifest_peek(&s->files,i,row))return false;if(get(row+4))continue;bool found;if(!node_owner(s,get(row+8),node,&found))return false;
  if(found&&get(node+16)==8&&get(node+4)==get(row+8)){uint32_t candidate=get(node+20);bool seen;if(!cat422_manifest_get(&d->files,candidate,desc)||!seenbit(s,candidate,false,&seen))return false;
   if(!seen&&(get(desc)&65535)==(get(row)&255)){if(!seenbit(s,candidate,true,&seen))return false;put(row+4,candidate+1);if(!cat422_manifest_patch(&s->files,i,row))return false;}
  }
 }
 unsigned known=0;for(unsigned i=0;i<s->nd;i++)known+=s->dirs[i].role!=0;if(known!=146){s->input_error=3;return false;}if(!s->seen.sync(s->seen.ctx)||!cat422_manifest_finish(&s->files))return false;fat422_bitmap bitmap=arena422_bitmap_io(&d->arena);memset(s->sector,0,512);for(unsigned i=0;i<(d->files.count+4095)/4096;i++)if(!bitmap.write(bitmap.ctx,i,s->sector))return false;
 if(!overlay_visit(&d->overlay,dirty_visit,s)||!bitmap.sync(bitmap.ctx))return false;
 /*505: after complete FAT/rename/dirty-map validation, unchanged generated
  * native WAVs have exactly their immutable original chain/length and no
  * overlay data edits. Reuse that proof; inspect every added/replaced file.
  * This is not a filename/mtime shortcut and does not skip any FAT validation.
  * Decode remains outside the FAT walker's directory stack. */
 if(!d->ready)return false;s->ready=true;uint32_t modified=0;
 for(uint32_t i=0;i<s->files.count;i++){
  import422_edit edit;if(!discover422_get(s,i,&edit)){s->ready=false;return false;}if(!edit.action)continue;modified++;
  uint8_t row[32];if(!cat422_manifest_get(&s->files,i,row)){s->ready=false;return false;}
  source src={s,get(row+8),get(row+12)};wav_source wave={&src,file_read,src.bytes};
  if(!memcmp(row+24,"SPN",3)){
   /* Bound and bind the complete plan before allocation. Block validity and
    * aggregate CRC are checked while staging immutable payloads; the batch
    * root is never published until every source and stored readback passes. */
   native593_info native;
   if(!native593_probe(wave,&native,s->sector)||native.lane!=edit.lane||native.action!=edit.action||native.identity!=d->arena.identity||native.epoch!=d->arena.epoch||native.generation!=d->arena.generation){s->ready=false;s->input_error=2;memcpy(s->input_name,row+16,11);s->input_name[11]=0;return false;}
   uint32_t ordinal=0;if(get(row+4)){if(!cat422_manifest_get(&d->files,get(row+4)-1,s->sector)){s->ready=false;return false;}ordinal=get(s->sector+4);}
   if(native.ordinal!=ordinal){s->ready=false;s->input_error=2;return false;}
   reserve+=(((native.blocks+TAKE_HEADER_BLOCKS+15ull)/16u)*16u);continue;
  }
  wav_info info;wav_result result=wav_inspect(wave,&info);
  if(result!=WAV_OK){s->ready=false;if(result==WAV_IO)return false;s->input_error=2;memcpy(s->input_name,row+16,11);s->input_name[11]=0;return false;}
  reserve+=(((info.frames+279ull)/280+TAKE_HEADER_BLOCKS+15u)/16u)*16u;
  if(info.rate!=48000||info.format!=1||info.bits!=16||(info.codec!=0&&info.codec!=3))s->directory_valid|=DISCOVER510_GENERAL_WORK;
 }
 if(reservation&&reserve<=UINT32_MAX)*reservation=(uint32_t)reserve;
 if(changes)*changes=modified;return true;
}
bool discover505_build(discover422*s,drive422*d,uint32_t*changes){return discover512_build(s,d,changes,0);}
bool discover422_build(discover422*s,drive422*d){return discover505_build(s,d,0);}
/* Source contexts are caller-owned, so an edit reader can never retain a stack
 * pointer. Full import enumeration/reopen binds these spool rows separately. */
bool discover422_get(discover422*s,uint32_t id,import422_edit*out){
 if(!s||!s->ready||!out)return false;uint8_t b[32],old[32];if(!cat422_manifest_get(&s->files,id,b))return false;memset(out,0,sizeof *out);out->lane=get(b)&255;uint32_t base=get(b+4);out->action=base?IMPORT422_REPLACE:IMPORT422_ADD;
 if(base){if(!cat422_manifest_get(&s->drive->files,base-1,old))return false;out->archive=get(old+28)>>9;out->audio=get(old+8);out->annotation=get(old+12);
  uint8_t node[32];bool dirty;if(!cat422_manifest_get(&s->drive->nodes,212+base-1,node)||!dirtybit(s,base-1,false,&dirty))return false;
  if(!dirty&&get(b+8)==get(node+4)&&get(b+12)==get(node+12)){
   bool same=true;uint32_t cl=get(b+8),count=get(node+8);for(uint32_t n=0;n<count;n++){uint32_t next;if(!chain(s,cl,&next))return false;if(n+1==count){if(next<EOC)same=false;}else if(next!=cl+1)same=false;cl=next;if(!same)break;}
   if(same)out->action=0;
  }
 }return true;
}

bool discover422_source_open(discover422*s,uint32_t id,discover422_source*ctx,wav_source*out){
 if(!s||!s->ready||!ctx||!out)return false;uint8_t b[32];if(!cat422_manifest_get(&s->files,id,b))return false;*ctx=(discover422_source){s,get(b+8),get(b+12)};*out=(wav_source){ctx,file_read,ctx->bytes};return true;
}

int discover422_deleted(discover422*s,uint32_t id,import422_edit*out){
 bool seen;if(!s||!out||!discover422_seen(s,id,&seen))return -1;if(seen)return 0;uint8_t b[32];if(!cat422_manifest_get(&s->drive->files,id,b))return -1;memset(out,0,sizeof *out);out->action=IMPORT422_DELETE;out->lane=get(b)&65535;out->archive=get(b+28)>>9;out->audio=get(b+8);out->annotation=get(b+12);return 1;
}
