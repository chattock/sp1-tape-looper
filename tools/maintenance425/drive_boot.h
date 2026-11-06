#ifndef SP1_DRIVE_BOOT_H
#define SP1_DRIVE_BOOT_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {uint32_t since;bool held,pending,spent;} drive_boot_gesture;
/* Any quiescent maintenance/recovery state may enter the flashing bootloader while the
 * persistent gate remains. It may never launch musical runtime this way. */
static inline bool drive_boot_admit570(bool blocked,bool autonomous,bool fault,bool mounted,bool upload){(void)blocked;(void)autonomous;(void)fault;return !mounted&&!upload;}
/* Match runtime's exact T1+T4 band and three-second hold. Intent survives release while
 * Finder ejects and the device commits: never detach a host-owned volume. */
static inline bool drive_boot_step(drive_boot_gesture*g,uint32_t now,int raw,bool fn_released,bool mounted,bool upload){
 if(g->pending)return !mounted&&!upload; /* Accepted intent survives release until safe completion. */
 if(raw<1256||raw>=1347||!fn_released){g->held=false;if(fn_released&&raw>=0&&raw<110)g->spent=false;return false;}
 if(g->spent)return false;
 if(!g->held){g->held=true;g->since=now;}
 if((uint32_t)(now-g->since)<3000)return false;
 g->pending=true;g->spent=true;return !mounted&&!upload;
}
#endif
