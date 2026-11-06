#ifndef CAT422_ARENA_H
#define CAT422_ARENA_H
#include "../paged/manifest.h"
#include "fat.h"
#include "../../commit421/runtime/src/pool.h"
#include "../../library-writeback/overlay.h"
enum {ARENA422_PREPARING=1,ARENA422_READY,ARENA422_COMMITTING,ARENA422_COMMITTED,ARENA422_ABORTED};
/* WORK.BIN is separately allocated by the FAT planner, NEVER inside AUDIO.BIN.
 * A persistent boot gate must hold throughout the lifetime of staged edits.
 * No function here unlocks the gate or automatically resumes a transaction. */
typedef bool (*arena422_locked)(void*,uint64_t identity,uint32_t work_first);
typedef struct arena422 arena422;
typedef struct {arena422 *arena;uint32_t offset,blocks;} arena422_slice;
struct arena422 {pool_io raw;uint32_t first,blocks,state,active;uint64_t identity,generation,sequence,epoch;arena422_locked locked;void *gate;bool ready;uint8_t sector[512];arena422_slice slices[4];};
bool arena422_latest(arena422*,pool_io,uint32_t,uint32_t,uint64_t,arena422_locked,void*);
bool arena422_create(arena422*,pool_io,uint32_t first,uint32_t blocks,uint64_t identity,uint64_t generation,arena422_locked,void*);
bool arena422_open(arena422*,pool_io,uint32_t first,uint32_t blocks,uint64_t identity,uint64_t generation,arena422_locked,void*);
/* Explicit new session from a terminal one. Caller has verified the published
 * catalog and retains the gate. Never renew READY/COMMITTING evidence. */
bool arena422_renew(arena422*,uint64_t verified_generation);
bool arena422_transition(arena422*,unsigned state);
pg422_io arena422_manifest_io(arena422*,unsigned which);
fat422_bitmap arena422_bitmap_io(arena422*);
ov_io arena422_overlay_io(arena422*,uint32_t virtual_sectors);
#endif
