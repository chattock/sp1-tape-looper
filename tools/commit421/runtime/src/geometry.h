#ifndef MUSIC_GEOMETRY366_H
#define MUSIC_GEOMETRY366_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {uint32_t frames,start_frames;} music_geometry;
/* Same legacy rounding and validated exact-anchor preference, without unchecked
 * multiplication/rounding overflow. Caller supplies the admitted capacity. */
bool music_geometry_load(uint32_t fpb,uint32_t song_frames,uint32_t loop_blocks,
 uint32_t start_blocks,uint32_t exact_frames,uint32_t exact_start,bool exact_valid,
 uint32_t capacity,music_geometry *out);
#endif
