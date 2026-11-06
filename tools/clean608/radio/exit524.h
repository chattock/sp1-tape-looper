#ifndef SETUP_EXIT524_H
#define SETUP_EXIT524_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {uint32_t since;bool held,spent;} setup_exit524;
static inline bool setup_exit524_step(setup_exit524*s,uint32_t now,int raw,bool valid,bool fn_released,bool safe){
 if(!valid||!fn_released||!safe){s->held=false;s->spent=true;return false;}
 if(raw<925||raw>=1044){s->held=false;s->spent=false;return false;}
 if(s->spent)return false;
 if(!s->held){s->held=true;s->since=now;return false;}
 if((uint32_t)(now-s->since)<3000)return false;
 s->spent=true;return true;
}
#endif
