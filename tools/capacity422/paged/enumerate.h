#ifndef CAT422_ENUMERATE_H
#define CAT422_ENUMERATE_H
#include "catalog.h"
/* Maintenance only, exclusive ownership. Constant RAM, no file-count table.
 * Callback may read the index, but must not mutate it. Each distinct saved
 * version is emitted once, followed by an unsaved live version if needed. */
typedef struct {uint32_t lane,ordinal,archive,audio,annotation;bool current;} cat422_entry;
typedef bool (*cat422_emit)(void*,const cat422_entry*);
bool cat422_enumerate(pg422*,unsigned lane,cat422_emit,void*,uint32_t *count);
#endif
