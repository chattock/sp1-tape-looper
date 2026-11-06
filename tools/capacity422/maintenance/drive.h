#ifndef CAT422_DRIVE_H
#define CAT422_DRIVE_H
#include "arena.h"
#include "../paged/view.h"
typedef struct {arena422 arena;cat422_manifest files,nodes;cat422_view view;overlay overlay;bool ready;} drive422;
/* Caller admits FAT ownership and holds the persistent gate first. Preparation
 * writes only WORK.BIN. Source catalog/audio remain unchanged. */
bool drive422_prepare(drive422*,cat422_workspace*,pool_io,uint32_t work_first,uint32_t work_blocks,uint64_t identity,arena422_locked,void*gate);
bool drive422_reopen(drive422*,pool_io,uint32_t work_first,uint32_t work_blocks,uint64_t identity,uint64_t source_generation,arena422_locked,void*gate);
bool drive422_read(drive422*,uint32_t,void*,uint32_t);
bool drive422_write(drive422*,uint32_t,const void*,uint32_t);
/* Freezes host edits; caller must then validate the overlay and journal/apply
 * its complete edit plan. This does NOT claim musical publication succeeded. */
bool drive422_freeze(drive422*);
/* Publication coordinator calls finish ONLY after its full validated edit plan
 * and payload checks have committed. Reopening the exact expected generation
 * is mandatory; this does not substitute for edit-plan validation. */
bool drive422_finish(drive422*,cat422_workspace*,uint64_t expected_generation);
/* Discard only uncommitted host staging, with unchanged source catalog. */
bool drive422_abort(drive422*,cat422_workspace*);
/* Rebuild the view after a terminal session; never overwrite unresolved edits. */
bool drive422_next(drive422*,cat422_workspace*);
#endif
