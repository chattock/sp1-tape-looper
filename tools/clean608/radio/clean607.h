#ifndef CLEAN607_H
#define CLEAN607_H
#ifdef SP1_BENCH_NATIVE_TEST
#define CLEAN607_CODE
#else
#define CLEAN607_CODE __attribute__((section(".exit524.text.clean607"),noinline))
#endif
/* The audited static sector, including its unique address, is never writable.
 * Primary VS is target-owned application settings in this profile; backup VS
 * is reset alongside the primary store. No eMMC dependency, donor identity or chip erase. */
static bool CLEAN607_CODE clean607_profile(void){
 bool zero=true,ff=true;for(unsigned i=0;i<6;i++){zero&=local_address[i]==0;ff&=local_address[i]==255;}
 return admission_complete(&admission)&&admission.layout_ok&&!zero&&!ff;
}
static void CLEAN607_CODE clean607_vs(void){
 memset(expected_vs,255,sizeof expected_vs);
 for(unsigned kind=0;kind<2;kind++){
  unsigned at=kind*184;uint8_t*r=expected_vs+at+4;memset(r,0,180);memcpy(r,"TLB2",4);r[4]=2;r[5]=kind;r[6]=kind?65:138;
  memcpy(r+8,local_address,6);memcpy(r+14,target_abi,16);uint32_t crc=sg_crc(r,176);for(unsigned i=0;i<4;i++)r[176+i]=crc>>(8*i);
  unsigned sum=0;for(unsigned i=0;i<180;i++)sum+=r[i];unsigned check=(sum^0x55)&255,tag=0xf0+kind;
  expected_vs[at]=tag;expected_vs[at+1]=180;expected_vs[at+2]=check;expected_vs[at+3]=((tag+180+check)^0x55)&255;
 }
}
static bool CLEAN607_CODE clean607_admit(void){
 if(!clean607_profile()||memcmp(after_hashes[2],erased_hash,32))return false;
 /* Healthy current target is a no-op: keep existing valid bound pairings.
  * Every repair/replacement constructs a fresh deterministic settings store. */
 if(memcmp(before_hashes[2],erased_hash,32)||memcmp(admission.app_hash,target_hash,32)||!vs_target()||memcmp(expected_vs,original_vs,4096))clean607_vs();
 uint8_t h[32];sector_hash(expected_vs,h);if(memcmp(h,after_hashes[1],32))return false;
 for(unsigned i=0;i<27;i++)if(memcmp(after_hashes[i+3],target_sectors_hash[i],32))return false;
 return true;
}
#endif
