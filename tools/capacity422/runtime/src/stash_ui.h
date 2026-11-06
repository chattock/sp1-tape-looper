#ifndef STASH_UI_H
#define STASH_UI_H
#include <stdint.h>
/* Pure edge decoder. Neutral must be observed before accepting another action;
 * entering mode with a held rocker, changing targets mid-hold, and FN are inert. */
typedef struct {uint8_t enabled:1,latched:2;} stash_ui;
static inline unsigned stash_ui_step(stash_ui *s,int enabled,int blocked,int direction,int track){
 if(!enabled){s->enabled=0;s->latched=1;return 0;}
 if(!s->enabled){s->enabled=1;s->latched=direction!=0;return 0;}
 if(!direction){s->latched=0;return 0;}
 if(s->latched)return 0;
 if(track<0||track>=4){s->latched=2;return 0;} /* bare rocker owns this press */
 s->latched=1;if(blocked)return 0;
 return direction<0?1u:2u;
}
static inline int stash_pick(const uint16_t refs[64],int start,int direction,int empty){
 if(start < -1 || start > 63 || (direction!=-1 && direction!=1))return -1;
 for(unsigned n=0;n<64;n++){
  int slot=empty?(int)n:(start+direction*(int)(n+1)+128)%64;
  if((refs[slot]!=UINT16_MAX)!=!!empty)return slot;
 }
 return -1;
}
#endif
