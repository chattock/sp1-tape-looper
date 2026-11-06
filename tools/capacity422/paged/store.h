#ifndef SP1_PAGED422_H
#define SP1_PAGED422_H
#include <stdint.h>
#include <stdbool.h>
/* Sector-addressed COW index. External file allocation/ownership is mandatory.
 * Never call from audio thread. Records contain IDs/extents, never RAM pointers.
 * Up to123*123*15 entries; RAM usage independent of number of records. */
#define PG422_FANOUT 123u
#define PG422_RECORDS 15u
#define PG422_LIMIT (PG422_FANOUT*PG422_FANOUT*PG422_RECORDS)
typedef struct {void *ctx;bool (*read)(void*,uint32_t,void*);bool (*write)(void*,uint32_t,const void*);bool (*sync)(void*);uint32_t sectors;} pg422_io;
/*426: operation-local, checked page cache. Lives inside the existing storage
 * overlay, never retained through recording or used by the audio thread.
 * Root/branch have dedicated slots; three leaves cover lanes/header/extents. */
typedef struct {pg422_io io;uint64_t generation;uint32_t root,tail,transaction_first;unsigned active;bool ready,transaction;uint8_t scratch[512],walk_root[512],walk_branch[512];uint32_t cache_at[5];unsigned cache_next;uint8_t cache[5][512];} pg422;
/* Blank store creation is explicitly separate from open; never autoformat. */
bool pg422_create(pg422*,pg422_io);
bool pg422_open(pg422*,pg422_io);
bool pg422_get(pg422*,uint32_t id,uint8_t record[32]);
bool pg422_begin(pg422*);
bool pg422_stage(pg422*,uint32_t id,const uint8_t record[32]);
bool pg422_commit(pg422*);
/* Discard staged changes by reopening durable checkpoints. No writes. */
bool pg422_abort(pg422*);
bool pg422_put(pg422*,uint32_t id,const uint8_t record[32]);
/* Offline, disjoint-file compaction. Caller journals file publication. */
bool pg422_compact(pg422 *source,pg422 *destination,pg422_io target);
/* Callback must not re-enter this index; record pointer expires on return. */
typedef bool (*pg422_visit)(void*,uint32_t,const uint8_t[32]);
bool pg422_walk(pg422*,pg422_visit,void*);
bool pg422_checkpoint(pg422*,pg422_io,unsigned slot);
bool pg422_repair_inactive(pg422*);
/* Probe bank headers without following stale pages in a retired bank. False
 * means I/O or structurally invalid checksummed metadata, never "empty". */
bool pg422_probe(pg422*,pg422_io,bool *valid,uint64_t *generation);
#endif
