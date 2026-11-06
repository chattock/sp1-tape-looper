#ifndef STASH382_HANDOFF_H
#define STASH382_HANDOFF_H
#include <stdint.h>
#include <stdbool.h>
#define STASH_DRAIN (1u<<16)
#define STASH_DRAINED (1u<<17)
#define STASH_PUBLISH (1u<<18)
#define STASH_PUBLISHED (1u<<19)
#define STASH_CHANGED (1u<<20)
enum {STASH_IDLE,STASH_FADE,STASH_DETACH,STASH_APPLY};
static inline unsigned stash_handoff_step(uint32_t q,bool silent){
 if(q&STASH_PUBLISHED)return STASH_IDLE;
 if((q&STASH_PUBLISH)&&!(q&STASH_PUBLISHED))return STASH_APPLY;
 if((q&STASH_DRAIN)&&!(q&STASH_DRAINED))return silent?STASH_DETACH:STASH_FADE;
 return STASH_IDLE;
}
#endif
