#include "../fresh529/fresh.h"
#define FRESH529_FN __attribute__((section(".fresh529.text.adapter"),noinline))
_Static_assert(sizeof(fresh529_work)<=sizeof(shared),"initializer must borrow existing maintenance scratch");
static FRESH529_FN void fresh529_progress(void*c,unsigned phase,uint32_t done,uint32_t total){
 ARG_UNUSED(c);feed();uint32_t p=phase==1?8:phase==2?16+(total?done*176u/total:0):phase==3?208:255;
 for(unsigned i=0;i<4;i++)lamp(i,p>i*64u);
}
static FRESH529_FN int fresh529_boot(void){
 const fresh529_request*r=(const void*)FRESH529_ADDRESS;
 const volatile uint32_t*wake=(const void*)(FRESH529_ADDRESS+sizeof(*r));
 if(fresh529_erased(r)|| (fresh529_authorized(r)&&!fresh529_pending(r))){
  if(*wake==0xffffffffu||*wake==0u)return 0;
  /* A STANDARD reflash must still inspect/recover a retained drive gate.
   * Defer its one-shot musical wake; never bypass or clear the gate. */
  if(sp1_gate_blocked(gate_page))return 0;
  if(NRF_POWER->GPREGRET2==0xa5u||!device_is_ready(gate_flash))return -1;
  uint32_t zero=0;if(flash_write(gate_flash,FRESH529_ADDRESS+sizeof(*r),&zero,4))return -1;
  __DSB();if(*wake)return -1;
  NRF_POWER->RESETREAS=0xffffffffu;NRF_POWER->GPREGRET=0;NRF_POWER->GPREGRET2=0xa9u;__DSB();NVIC_SystemReset();return -1;
 }
 if(!fresh529_authorized(r))return -1;
 if(!fresh529_pending(r))return 0;
 /* A different unfinished transaction is never discarded by a mode gesture. */
 if(sp1_gate_blocked(gate_page))return 0; /* Recover the retained transaction before honoring a pending wipe. */
 if(NRF_POWER->GPREGRET2==0xa5u||!device_is_ready(gate_flash))return -1;
 if(!identify()||!emmc_cache_enable())goto incomplete;
 cache_on=true;
 fresh529_io io={NULL,library_rd,library_wr,library_sync,fresh529_progress};
 if(!fresh529_initialize(io,capacity,&shared529.fresh)||!auto_geometry()||!editor422_verify(&workspace,auto_raw,2)||!emmc_cache_flush())goto incomplete;
 uint32_t complete=0;
 if(flash_write(gate_flash,FRESH529_ADDRESS+offsetof(fresh529_request,complete),&complete,4))goto incomplete;
 __DSB();if(r->complete)goto incomplete;
 uint32_t zero=0;if(*wake!=0xffffffffu&&*wake!=0u){if(flash_write(gate_flash,FRESH529_ADDRESS+sizeof(*r),&zero,4))goto incomplete;__DSB();if(*wake)goto incomplete;}
 emmc_power_down();NRF_POWER->RESETREAS=0xffffffffu;NRF_POWER->GPREGRET=0;NRF_POWER->GPREGRET2=0xa9u;__DSB();NVIC_SystemReset();return -1;
incomplete:
 /* Nothing has been exposed to the player. Discard partial volatile format
  * state; the durable request enforces a full retry. Keep FN/bootloader
  * recovery usable even when a failed card cannot complete a flush. */
 cache_on=false;emmc_power_down();return -1;
}
