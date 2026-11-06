#ifndef GATE607_H
#define GATE607_H
/* Dedicated controller page, outside all code, media gates, identity and music.
 * Any non-erased word blocks musical boot. No irreplaceable data is stored:
 * target is pinned by this controller; identity/config derive from untouched
 * radio static fields. A torn marker/erase routes back to the same updater. */
#define CLEAN607_GATE 0xde000u
static bool __attribute__((section(".exit524.text.gate607"),noinline)) clean607_page_pending(void){const volatile uint32_t*p=(const void*)CLEAN607_GATE;for(unsigned i=0;i<1024;i++)if(p[i]!=UINT32_MAX)return true;return false;}
bool __attribute__((section(".exit524.text.gate607"),noinline)) clean607_pending(void){return NRF_POWER->GPREGRET2==0xa4u||clean607_page_pending();}
static bool __attribute__((section(".exit524.text.gate607"),noinline)) gate607_ready(void){uint32_t start=k_cycle_get_32();while(!NRF_NVMC->READY){for(unsigned i=0;i<8;i++)NRF_WDT->RR[i]=WDT_RR_RR_Reload;if((uint32_t)(k_cycle_get_32()-start)>k_ms_to_cyc_ceil32(500))return false;}return true;}
bool __attribute__((section(".exit524.text.gate607"),noinline)) clean607_begin(void){
 if(*(volatile uint32_t*)CLEAN607_GATE==0)return true;
 if(!gate607_ready())return false;NRF_NVMC->CONFIG=NVMC_CONFIG_WEN_Wen;if(!gate607_ready()){NRF_NVMC->CONFIG=NVMC_CONFIG_WEN_Ren;return false;}
 *(volatile uint32_t*)CLEAN607_GATE=0;bool ok=gate607_ready();NRF_NVMC->CONFIG=NVMC_CONFIG_WEN_Ren;__DSB();return ok&&*(volatile uint32_t*)CLEAN607_GATE==0;
}
bool __attribute__((section(".exit524.text.gate607"),noinline)) clean607_clear(void){
 if(!clean607_page_pending())return true;if(!gate607_ready())return false;NRF_NVMC->CONFIG=NVMC_CONFIG_WEN_Een;
 if(!gate607_ready()){NRF_NVMC->CONFIG=NVMC_CONFIG_WEN_Ren;return false;}NRF_NVMC->ERASEPAGE=CLEAN607_GATE;bool ok=gate607_ready();NRF_NVMC->CONFIG=NVMC_CONFIG_WEN_Ren;__DSB();return ok&&!clean607_page_pending();
}
#endif
