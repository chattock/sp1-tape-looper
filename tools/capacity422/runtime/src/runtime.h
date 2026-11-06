#ifndef SP1_MUSIC_RUNTIME_H
#define SP1_MUSIC_RUNTIME_H
#include "session.h"
/* Adapter for the maintained looper's virtual fixed-slot read addresses.
 * The selected song's four extents are retained by music_session. No FAT or
 * catalog lookup, copying, allocation, or I/O on a playback mapping call.
 * Map failure must NEVER fall through to a raw physical read. */
bool music_runtime_map(const music_session *,uint32_t virtual_lba,uint32_t blocks,
                       uint32_t legacy_first,uint32_t legacy_stride,
                       uint32_t *physical,uint32_t *mapped);
/* Native callers keep their original sample timeline and block wrap. This
 * window helper caps at the explicit stored wrap independently of reservation.
 * Reverse mapping returns an ascending range to consume backwards. */
bool music_runtime_window(const music_session *,unsigned track,
                          const take_metadata *,uint32_t offset,uint32_t wanted,
                          bool reverse,uint32_t *physical,uint32_t *mapped);
#endif
