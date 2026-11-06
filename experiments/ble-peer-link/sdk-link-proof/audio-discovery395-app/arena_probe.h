#pragma once
#include <stdint.h>
#include <string.h>
#include "wiced_memory.h"
#define SP1_ARENA_BYTES 32768u
#define SP1_ARENA_GUARD 16u
/* One timer owner. Never called from audio or GATT callbacks. Pattern is
 * diagnostic data only; no NVRAM/flash access. Permanent until radio reset. */
static volatile uint8_t *arena;
static uint32_t arena_before,arena_min=UINT32_MAX,arena_done,arena_cursor,arena_passes,arena_faults,arena_attempted;
static wiced_bt_buffer_statistics_t arena_pools[4];
static uint8_t arena_pattern(unsigned i){return (uint8_t)((i*73u)^(i>>8)^0xa7u);}
static void arena_probe_step(unsigned mode){
 if(!mode)return; /* initialize profile allocations before reserving */
 uint32_t free_now=wiced_memory_get_free_bytes();
 if(!arena_attempted){
  arena_attempted=1;arena_before=free_now;
  /* Conservative admission guard, not proof16KiB is sufficient headroom. */
  if(free_now<SP1_ARENA_BYTES+SP1_ARENA_GUARD+16384u)arena_faults|=1;
  else {arena=wiced_memory_permanent_allocate(SP1_ARENA_BYTES+SP1_ARENA_GUARD);if(!arena)arena_faults|=2;}
  if(arena)for(unsigned i=0;i<8;i++){arena[i]=0x5a;arena[8+SP1_ARENA_BYTES+i]=0xa5;}
 }
 free_now=wiced_memory_get_free_bytes();if(free_now<arena_min)arena_min=free_now;
 if(arena){
  for(unsigned i=0;i<8;i++)if(arena[i]!=0x5a||arena[8+SP1_ARENA_BYTES+i]!=0xa5)arena_faults|=4;
  unsigned start=arena_done<SP1_ARENA_BYTES?arena_done:arena_cursor;
  for(unsigned i=start;i<start+1024;i++){
   if(arena_done<SP1_ARENA_BYTES)arena[8+i]=arena_pattern(i);
   if(arena[8+i]!=arena_pattern(i))arena_faults|=8;
  }
  if(arena_done<SP1_ARENA_BYTES){arena_done+=1024;if(arena_done==SP1_ARENA_BYTES)++arena_passes;}
  else {arena_cursor+=1024;if(arena_cursor==SP1_ARENA_BYTES){arena_cursor=0;++arena_passes;}}
 }
 memset(arena_pools,0,sizeof arena_pools);
 if(wiced_bt_get_buffer_usage(arena_pools,sizeof arena_pools)!=WICED_BT_SUCCESS)arena_faults|=16;
 for(unsigned i=0;i<4;i++)if(arena_pools[i].current_allocated_count>arena_pools[i].total_count||arena_pools[i].max_allocated_count>arena_pools[i].total_count)arena_faults|=32;
}
static void arena_probe_report(unsigned mode){
 uint32_t report[10]={0x314d454d,1,arena_before,arena_min,arena?SP1_ARENA_BYTES:0,arena_done,arena_passes,arena_faults,mode,arena_attempted};
 wiced_transport_send_data(0x7e02,(uint8_t*)report,sizeof report);
 wiced_transport_send_data(0x7e03,(uint8_t*)arena_pools,sizeof arena_pools);
}
