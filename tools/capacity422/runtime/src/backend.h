#ifndef SP1_RUNTIME_BACKEND_H
#define SP1_RUNTIME_BACKEND_H
#include "music_workspace.h"
#include "admit.h"
typedef enum { MUSIC_MEDIA_INVALID, MUSIC_MEDIA_LEGACY, MUSIC_MEDIA_POOL } music_media;
/* Storage-thread-only mount. Caller supplies exclusive raw I/O and the existing
 * stopped batch workspace. No format/repair/write is attempted. Legacy detection
 * never authorizes pool operations; caller keeps its existing legacy backend.
 * Mounted FAT state never survives this call. */
music_media music_backend_mount(music_runtime_retained *,music_runtime_workspace *,pool_io raw,bool stopped_drained);
/* Read controls once while validating their complete CRC.
 * Caller applies only after all metadata and song load have succeeded, before
 * resuming audio. Output is caller's existing six-sector metadata buffers. */
bool music_backend_controls(music_runtime_retained *,music_runtime_workspace *,uint8_t out[3072],bool stopped_drained);
bool music_backend_song(music_runtime_retained *,music_runtime_workspace *,unsigned song,bool stopped_drained);
/* One serialized catalog open for controls plus selected-song metadata. No
 * pointers into this workspace survive the call. Caller still cross-checks
 * song geometry before applying controls or permitting playback. */
bool music_backend_load(music_runtime_retained *,music_runtime_workspace *,unsigned song,bool stopped_drained);
#endif
