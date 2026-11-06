#ifndef SP1_RADIO_KEYS579_H
#define SP1_RADIO_KEYS579_H
#include <stdbool.h>
#include <stdint.h>
/* Replaces the existing eight-byte stack gesture state. No persistent state,
 * IRQ or radio-write behavior. Recognition and destination admission differ. */
typedef struct {uint32_t since;uint8_t kind,pending,spent;} radio_keys579;
static inline unsigned radio_keys579_step(radio_keys579*s,uint32_t now,int raw,bool valid,bool fn_released,bool active,bool music_safe){
 if(s->pending){
  /* Protected bootloader scans physical buttons at reset. Do not execute a
   * deferred track chord after its release, including across a transaction. */
  if(!valid)return 0;
  bool held=fn_released&&(s->pending==2?(raw>=1256&&raw<1347):(raw>=925&&raw<1044));
  if(!held){s->pending=0;s->kind=0;s->spent=1;if(fn_released&&raw>=-16&&raw<110)s->spent=0;return 0;}
  return active?0:(s->pending==2||music_safe?s->pending:0);
 }
 if(!fn_released){s->kind=0;s->spent=1;return 0;}
 if(!valid){s->kind=0;return 0;} /* A missed sample restarts timing, never fabricates a consumed chord. */
 unsigned kind=raw>=1256&&raw<1347?2:raw>=925&&raw<1044?1:0;
 if(!kind){s->kind=0;if(raw>=-16&&raw<110)s->spent=0;return 0;}
 if(s->spent)return 0;
 if(s->kind!=kind){s->kind=kind;s->since=now;return 0;}
 if((uint32_t)(now-s->since)<3000)return 0;
 /* A failed radio must never be returned to audio by an exit chord. */
 if(kind==1&&!music_safe){s->spent=1;s->kind=0;return 0;}
 s->pending=kind;s->spent=1;
 return active?0:kind;
}
#endif
