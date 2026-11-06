#ifndef LIBRARY_EDIT_WAV_H
#define LIBRARY_EDIT_WAV_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef bool (*wav_read)(void *,uint32_t offset,void *,uint32_t bytes);
typedef struct {void *ctx;wav_read read;uint32_t bytes;} wav_source;
/* frames is canonical48k output length; source_frames/rate describe the input. */
typedef struct {uint32_t data_offset,data_bytes,frames;uint16_t channels,bits,format;uint32_t source_frames,rate;uint8_t big_endian,codec;} wav_info;
typedef enum {WAV_OK,WAV_IO,WAV_LAYOUT,WAV_FORMAT,WAV_EMPTY,WAV_COMPLEX} wav_result;
/* Strict bounded audio probe/preflight. PCM WAV/extensible, AIFF PCM, MP3,
 * native FLAC; mono/stereo at qualified rates<=96k, output<=10minutes. */
wav_result wav_inspect(wav_source,wav_info *);
/* Immutable source staging can reuse discovery's full preflight guarantee. */
wav_result wav_probe469(wav_source,wav_info *);
bool wav_frames(wav_source,const wav_info *,uint32_t first,uint32_t count,int16_t *stereo,uint32_t capacity_frames);
bool wav_appledouble(wav_source);
/* Maintenance-exclusive borrowed admission scratch, aligned by converter.
 * Valid only after geometry admission and until the next admission pass. */
void *wav_work469(size_t *bytes);
void wav_progress469(unsigned phase,uint32_t done,uint32_t total);

/* Canonical PCM16 may borrow only the encoder prefix without invalidating the frozen index. */
void *wav_native_work509(size_t *bytes);
/* Uncompressed PCM/float only:8KiB tail disjoint from converter state. */
void *wav_output_work510(const wav_info*);
#endif
