#ifndef CAT422_WAV_H
#define CAT422_WAV_H
#include "directory.h"
/* Maintenance only. Constant memory regardless of take count or duration.
 * Reads the native payload directly; does not pre-render or write WAV copies. */
typedef struct {
 pool_io raw;cat422_manifest *manifest;
 uint32_t file,block,first,blocks,frames,spb,bytes;
 bool ready,cached;
 uint8_t encoded[512],next[512];int16_t pcm[992];
} cat422_wav;
bool cat422_wav_open(cat422_wav*,cat422_manifest*,pool_io,uint32_t file);
bool cat422_wav_read(cat422_wav*,uint32_t offset,void*,uint32_t length);
#endif
