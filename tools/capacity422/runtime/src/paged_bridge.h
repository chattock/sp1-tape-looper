#ifndef PAGED_BRIDGE422_H
#define PAGED_BRIDGE422_H
#include "engine.h"
#include "../../paged/session.h"
#include "../../paged/preview.h"
bool music_backend_paged(void);
bool bridge422_open(music_runtime_retained*,music_runtime_workspace*);
bool bridge422_load(music_runtime_retained*,music_runtime_workspace*,unsigned);
bool bridge422_preflight(music_runtime_retained*,music_runtime_workspace*);
bool bridge422_prepare(music_runtime_retained*,music_runtime_workspace*,unsigned,uint32_t,bool);
/* Same serialized operation immediately after music_engine_load; no workspace
 * reuse, bank change, or intervening storage operation is allowed. */
bool bridge426_prepare_loaded(music_runtime_retained*,music_runtime_workspace*,unsigned,uint32_t,bool);
bool bridge422_ready(music_runtime_retained*,music_runtime_workspace*);
bool bridge422_finish(music_runtime_retained*,music_runtime_workspace*,const take_metadata*,music_control_snapshot,void*);
bool bridge422_save(music_runtime_retained*,music_runtime_workspace*,music_control_snapshot,void*);
int bridge422_cycle(music_runtime_retained*,music_runtime_workspace*,unsigned,int,unsigned*);
bool bridge422_drop(music_runtime_retained*,music_runtime_workspace*,unsigned);
bool bridge422_publish(music_runtime_retained*,music_runtime_workspace*,unsigned);
bool bridge427_warm(music_runtime_retained*,music_runtime_workspace*,uint32_t);
#endif
