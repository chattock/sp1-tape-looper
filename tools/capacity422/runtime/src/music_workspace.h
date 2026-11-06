#ifndef SP1_RUNTIME_WORKSPACE360_H
#define SP1_RUNTIME_WORKSPACE360_H
#include "runtime.h"
#include "ff.h"
/* Layout contract for later main.c integration. Do NOT allocate a second copy.
 * This union must overlay g_bb only while playback/record/decode are stopped and
 * all callbacks have drained. The maintained buffer is only4-aligned; give the
 * actual overlay8-byte alignment before using pool_store (uint64 generations).
 * No pointers into this workspace may survive returning it to streaming. */
typedef union {
 uint8_t streaming_batch[16384];
 struct {pool_store pool;take_metadata loaded[4];uint8_t controls[3072];} stopped_song;
 struct {FATFS fs;FIL file;uint8_t scratch[1024];} mount;
} music_runtime_workspace;
typedef struct {music_session session;pool_io io;} music_runtime_retained;
_Static_assert(sizeof(music_runtime_workspace)==16384,"workspace must reuse existing batch exactly");
/* Current359 linked RAM margin is196. Full link/alignment/stack still required. */
_Static_assert(sizeof(music_runtime_retained)<=196,"retained state exceeds current margin");
#endif
