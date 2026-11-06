#ifndef SP1_CATALOG422_H
#define SP1_CATALOG422_H
#include "store.h"
/* Storage-coordinator operations only. Begin/stage/commit share the index
 * transaction with the matching control snapshot; never call in audio ISR. */
typedef struct {uint32_t first,blocks,crc,tag;} cat422_extent;
typedef struct {uint32_t audio,annotation,selected,head,tail,count,ordinal;} cat422_lane;
bool cat422_lane_read(pg422*,unsigned,cat422_lane*);
bool cat422_extent_read(pg422*,uint32_t,cat422_extent*);
bool cat422_begin(pg422*);
bool cat422_new_take(pg422*,unsigned);
bool cat422_cycle(pg422*,unsigned);
/* Coordinator-only selection of a target resolved from this open generation.
 * The caller validates the walk/ordinal; the catalog validates lane and refs.
 * Only an already archived or empty live lane is eligible. */
bool cat440_select(pg422*,unsigned lane,uint32_t selected,uint32_t ordinal);
bool cat422_delete(pg422*,unsigned);
bool cat422_add_extent(pg422*,cat422_extent,uint32_t*);
bool cat422_recorded(pg422*,unsigned,uint32_t audio,uint32_t annotation);
/* Change only the live metadata; the selected saved version remains immutable. */
bool cat422_annotated(pg422*,unsigned,uint32_t annotation);
bool cat422_preserve(pg422*,unsigned);
/* Maintenance edits: archive IDs must come from this generation's directory.
 * Caller stages controls and commits once after all imported payloads verify. */
bool cat422_live_replace(pg422*,unsigned lane,uint32_t audio,uint32_t annotation);
bool cat422_archive_add(pg422*,unsigned lane,uint32_t audio,uint32_t annotation);
bool cat422_archive_replace(pg422*,unsigned lane,uint32_t archive,uint32_t audio,uint32_t annotation);
bool cat422_archive_delete(pg422*,unsigned lane,uint32_t archive);
bool cat422_control(pg422*,uint32_t);
bool cat422_commit(pg422*);
#endif
