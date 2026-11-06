#ifndef MUSIC_SNAPSHOT366_H
#define MUSIC_SNAPSHOT366_H
#include "take.h"
/* Cross-check native song geometry against the immutable take objects before
 * publishing a pool-backed song. Does not apply any state or touch storage. */
bool music_native_song_validate(const uint8_t controls[3072],const take_metadata tracks[4],unsigned song);
/* Compute once for an immutable all-song snapshot. A missing/invalid X3 is
 * permitted only for empty songs, exactly as in music_native_song_validate. */
bool music_native_snapshot_extended(const uint8_t controls[3072]);
/* extended must come from the same unchanged controls buffer. Every base-header
 * and per-song geometry check remains; only the shared X3 checksum is reused. */
bool music_native_song_validate_prechecked(const uint8_t controls[3072],const take_metadata tracks[4],unsigned song,bool extended);
/*415: zero means valid; otherwise reason<<2 | track. No writes or extra reads. */
unsigned music_native_song_issue(const uint8_t controls[3072],const take_metadata tracks[4],unsigned song,bool extended);
bool music_native_song_normalize(uint8_t controls[3072],const take_metadata tracks[4],unsigned song,bool extended);
#endif
