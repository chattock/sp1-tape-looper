#ifndef SP1_EXCHANGE_POLICY_H
#define SP1_EXCHANGE_POLICY_H
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
typedef struct {uint32_t first,blocks,fat_sectors,data,pool_first,pool_blocks;} sp1_exchange;
static inline uint32_t exchange_u32(const uint8_t *b){return b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;}
/* A host may edit only exchange-space allocations and its directory entries.
 * Core geometry, SP1 directory, catalogs and the entire audio pool stay fixed.
 * Write protection is independent of filenames, attributes or OS behavior. */
static inline bool exchange_write_allowed(const sp1_exchange *g,uint32_t lba,const uint8_t before[512],const uint8_t after[512]){
 if(lba<g->first||lba-g->first>=g->blocks)return false;
 if(!memcmp(before,after,512))return true;
 if(lba<g->first+32){
  /* FAT32 free-count hints only; never permit a geometry/boot-code rewrite. */
  if(lba!=g->first+1&&lba!=g->first+7)return false;
  return !memcmp(before,after,488)&&!memcmp(before+496,after+496,16);
 }
 if(lba<g->data){
  uint32_t index=(lba-g->first-32)%g->fat_sectors;
  uint32_t host=8+g->pool_blocks/16,max=2+(g->first+g->blocks-g->data)/16;
  for(unsigned i=0;i<128;i++){
   uint32_t cl=index*128+i,old=exchange_u32(before+4*i),next=exchange_u32(after+4*i);
   if(old==next)continue;
   /* Observed macOS mount/eject toggles only FAT[1] clean-shutdown bit. */
   if(cl==1 && ((old^next)&~0x08000000u)==0)continue;
   if(cl<host||cl>=max||(next&0xf0000000u))return false;
   if(next&&next<0x0ffffff8u&&(next<host||next>=max))return false;
  }return true;
 }
 if(lba>=g->pool_first&&lba-g->pool_first<g->pool_blocks)return false;
 if(lba>=g->data+16&&lba<g->data+32)return false; /* SP1 directory */
 if(lba>=g->data+64&&lba<g->pool_first)return false; /* catalogs/slack */
 if(lba==g->data&&memcmp(before,after,96))return false; /* root's three entries */
 return true;
}
#endif
