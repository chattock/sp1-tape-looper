#include "admit.h"
#include "store_core.h"
#include <string.h>
typedef struct {FATFS *fs;pool_io io;uint8_t *scratch;uint32_t cached;bool have_cache;} fat_reader;
static uint32_t get32(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static bool entry(void *ctx,uint32_t cluster,uint32_t *value){
 fat_reader *r=ctx;uint32_t sector=cluster/128,offset=(cluster%128)*4;
 if(cluster>=r->fs->n_fatent||sector>=r->fs->fsize)return false;
 uint64_t a=(uint64_t)r->fs->fatbase+sector,b=a+r->fs->fsize;
 if(b>=r->io.device_blocks)return false;
 if(!r->have_cache||r->cached!=sector){
  if(!r->io.read(r->io.context,(uint32_t)a,r->scratch,1)||
     !r->io.read(r->io.context,(uint32_t)b,r->scratch+512,1))return false;
  r->cached=sector;r->have_cache=true;
 }
 uint32_t first=get32(r->scratch+offset)&0x0fffffff;
 if(first!=(get32(r->scratch+512+offset)&0x0fffffff))return false;
 *value=first;return true;
}
static bool admit(FATFS *fs,pool_io raw,pool_io *out,uint8_t scratch[1024],FIL *file,bool paged){
 if(!fs||!out||!scratch||!file||!raw.read||!raw.write||!raw.sync||fs->fs_type!=FS_FAT32||
    fs->n_fats!=2||fs->n_fatent<65527||!fs->csize||fs->csize>128||
    (fs->csize&(fs->csize-1))||fs->database>=raw.device_blocks)return false;
 uint64_t data_blocks=(uint64_t)(fs->n_fatent-2)*fs->csize;
 if(data_blocks>raw.device_blocks-fs->database)return false;
 sp1_volume data={raw.device_blocks,(uint32_t)fs->database,(uint32_t)data_blocks};
 fat_reader reader={.fs=fs,.io=raw,.scratch=scratch};sp1_extent e[3];
 /* Resolve once during admission, never in the audio/refill path. A present but
  * damaged new namespace must not silently fall back to an old library. */
 const char *names[]={"TAPELOOP/CAT0.BIN","TAPELOOP/CAT1.BIN","TAPELOOP/AUDIO.BIN"};
 FILINFO namespace_info;
 FRESULT ns=f_stat("TAPELOOP",&namespace_info);
 if(ns==FR_NO_FILE||ns==FR_NO_PATH){names[0]="SP1/CAT0.BIN";names[1]="SP1/CAT1.BIN";names[2]="SP1/AUDIO.BIN";}
 else if(ns!=FR_OK||!(namespace_info.fattrib&AM_DIR))return false;
 if(paged){
  if(ns!=FR_OK)return false;
  for(unsigned i=0;i<2;i++){FRESULT old=f_stat(names[i],&namespace_info);if(old!=FR_NO_FILE&&old!=FR_NO_PATH)return false;}
  names[0]="TAPELOOP/INDEX0.BIN";names[1]="TAPELOOP/INDEX1.BIN";
 }
 for(unsigned i=0;i<3;i++){
  if(f_open(file,names[i],FA_READ)!=FR_OK)return false;
  uint64_t bytes=f_size(file);uint32_t cluster=file->obj.sclust;
  bool valid=file->obj.fs==fs&&bytes&&bytes%512==0&&bytes/512<=UINT32_MAX&&
             (i==2?bytes>=16384:bytes==(paged?4194304u:4096u));
  if(f_close(file)!=FR_OK||!valid)return false;
  uint32_t blocks=(uint32_t)(bytes/512),clusters=(blocks+fs->csize-1)/fs->csize;
  if(!sp1_fat_contiguous(data,fs->csize,fs->n_fatent-2,cluster,clusters,entry,&reader,&e[i]))return false;
  /* Catalog files may occupy larger clusters, but the audio pool must consume
   * its whole reservation so allocation cannot expose FAT file slack. */
  if(i==2&&e[i].count!=blocks)return false;
 }
 if(!sp1_extents_disjoint(data,e,3))return false;
 raw.catalog[0]=e[0].first;raw.catalog[1]=e[1].first;raw.pool_first=e[2].first;raw.pool_blocks=e[2].count;
 *out=raw;return true;
}

bool music_fat_admit(FATFS *fs,pool_io raw,pool_io *out,uint8_t scratch[1024]){
 FIL file;return music_fat_admit_workspace(fs,raw,out,scratch,&file);
}

bool music_fat_admit_workspace(FATFS*f,pool_io r,pool_io*out,uint8_t s[1024],FIL*file){return admit(f,r,out,s,file,false);}
bool music_fat_admit_paged(FATFS*f,pool_io r,pool_io*out,uint8_t s[1024],FIL*file){return admit(f,r,out,s,file,true);}
