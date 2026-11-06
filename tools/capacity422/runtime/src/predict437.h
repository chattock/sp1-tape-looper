#ifndef PREDICT437_H
#define PREDICT437_H
#include "paged_bridge.h"
/* Stack-owned ticket: never points into reusable workspace. Coordinator only. */
typedef struct {uint64_t generation;sp1_extent extent;unsigned song,track,ordinal;int direction,result;uint32_t selected;bool fast440,unchanged440;} switch437_ticket;
int bridge437_predict(music_runtime_retained*,music_runtime_workspace*,unsigned,int,switch437_ticket*);
bool bridge437_finish(music_runtime_retained*,music_runtime_workspace*,const switch437_ticket*);
bool bridge440_extend(music_runtime_retained*,music_runtime_workspace*,switch437_ticket*,uint32_t actions,unsigned count);
#endif
