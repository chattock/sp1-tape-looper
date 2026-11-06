#ifndef SP1_TAKE_H
#define SP1_TAKE_H
#include <stdbool.h>
#include <stdint.h>
#include "pool.h"
#define TAKE_HEADER_BLOCKS 16u /*8KiB prefix preserves storage write-page alignment*/
#define TAKE_TAG 0x314b4154u
/* Explicit portable values, not a dump of compiler structs. Format1 is the
 * existing A7/P14S per-block dispatch (280 engine frames/block),2 is P16M(496). */
#define TAKE_FIELDS(X) \
 X(format) X(rate) X(frames_per_block) X(audio_blocks) X(frames) \
 X(content_blocks) X(start_frames) X(song_frames) X(speed_q16) \
 X(track_head_blocks) X(track_tail_blocks) X(song_head_frames) X(song_tail_frames) \
 X(grid_origin) X(beat_frames) X(bpm_q8) X(grid_beats) X(beats_per_bar) \
 X(pan) X(muted) X(chop_div) X(chop_offset) X(nudge) X(take_preset) \
 X(grid_offset_encoded) X(magnet_cells) X(content_flags) X(song_mode) X(loop_blocks)
typedef struct {
#define FIELD(name) uint32_t name;
 TAKE_FIELDS(FIELD)
#undef FIELD
 uint8_t magnet[32]; /* Effective256-cell mask, not pointers or live undo history. */
} take_metadata;
/* v2 preserves the native block wrap separately from the sample timeline.
 * Zero retains v1 behavior (ceil(frames/frames_per_block)). */
bool take_valid(const take_metadata *);
bool take_encode(const take_metadata *,uint8_t sector[512]);
bool take_decode(const uint8_t sector[512],take_metadata *);
/* Coordinator serializes these calls; prefix writes occur BEFORE live capture.
 * Final header can change actual take length after reservation, but musical
 * metadata is committed only together with the new audio object. */
bool take_begin(pool_store *,uint32_t capacity_audio_blocks,pool_writer *);
bool take_finish(pool_store *,pool_writer *,uint16_t destination,
                 const take_metadata *,bool replace);
/* Stage alongside a control snapshot; caller must finish_control before
 * exposing anything, or discard/reopen the uncommitted workspace. */
bool take_stage(pool_store *,pool_writer *,uint16_t,const take_metadata *,bool replace);
bool take_load(pool_store *,uint16_t ref,take_metadata *,pool_object *audio);
/* Staged metadata-only update; publish with the same control transaction. */
bool take_annotate(pool_store *,uint16_t ref,const take_metadata *);
#endif
