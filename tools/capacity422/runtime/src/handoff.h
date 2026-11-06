#ifndef SP1_SONG_HANDOFF_H
#define SP1_SONG_HANDOFF_H
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
/* Four retained bytes. Audio and storage acknowledge separately. No I/O,
 * allocation, waits or unbounded loops in the audio callback. */
typedef struct { atomic_uint word; } song_handoff;
_Static_assert(sizeof(unsigned)==4 && sizeof(song_handoff)==4,"32-bit lock-free state required");
void song_handoff_init(song_handoff *);
/* Control thread: request only while existing record/save work is quiet. */
uint32_t song_handoff_request(song_handoff *,unsigned song,bool stopped_saved);
/* Audio: enter silent/paused barrier before ack. Storage: finish/discard in-flight
 * reads and release batch scratch before ack. Ticket is from this request. */
bool song_handoff_ack(song_handoff *,uint32_t ticket,bool audio);
bool song_handoff_drained(const song_handoff *,uint32_t ticket,unsigned *song);
/* Storage publishes READY only after all metadata/maps are complete, or FAILED.
 * Audio applies that complete state and calls accept; it must not render the new
 * state before accept succeeds. A failed handoff never becomes playable. */
bool song_handoff_publish(song_handoff *,uint32_t ticket,bool success);
bool song_handoff_ready(const song_handoff *,uint32_t ticket);
bool song_handoff_accept(song_handoff *,uint32_t ticket);
uint32_t song_handoff_pending(const song_handoff *);
bool song_handoff_playable(const song_handoff *);
#endif
