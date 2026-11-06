#ifndef SP1_RELEASE583_H
#define SP1_RELEASE583_H
#include <stdbool.h>
#include <stdint.h>
/* Reuse the existing drive hold timestamp. Two confirmed idle samples are
 * required after startup/control handoff; no new mutable state. */
static inline bool drive583_wait_release(int64_t *since,int raw){
 if(*since>=-1)return false;
 if(raw>=0&&raw<110){if(*since==-3){*since=-1;return false;}*since=-3;}
 else *since=-2;
 return true;
}
#endif
