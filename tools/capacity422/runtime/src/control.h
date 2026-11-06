#ifndef SP1_MUSIC_CONTROL_H
#define SP1_MUSIC_CONTROL_H
#include "pool.h"
#define MUSIC_CONTROL_TAG 0x314c5443u
#define MUSIC_CONTROL_BLOCKS 6u
/* Exact maintained looper metadata sectors0..5. Native encoding remains
 * versioned by its existing magic/checksums. Caller validates semantics before
 * applying. Controls and take references publish in a single catalog commit.
 * Snapshot callback must be stable for all six calls (storage-thread barrier). */
typedef bool (*music_control_snapshot)(void *,unsigned sector,uint8_t out[512]);
bool music_control_save(pool_store *,music_control_snapshot,void *);
bool music_control_read(pool_store *,unsigned sector,uint8_t out[512]);
bool music_control_verify(pool_store *);
/* Read and verify once into the caller's existing six-sector buffer. The output
 * is private/unusable unless this returns true; semantic checks still follow.
 * The opened catalog and output must remain exclusively owned for this call. */
bool music_control_load(pool_store *,uint8_t out[MUSIC_CONTROL_BLOCKS*512]);
#endif
