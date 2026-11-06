#ifndef SP1_MUSIC_SESSION_H
#define SP1_MUSIC_SESSION_H
#include "take.h"
#include "control.h"
#include "store_core.h"
/* Retained state only. Catalog workspace is supplied by the storage thread and
 * may be reused for batch I/O between operations. No audio-thread allocation.
 * All calls must be serialized by that thread; not an IRQ-safe lock. */
typedef struct {
 sp1_ownership ownership;
 sp1_extent tracks[4];
 pool_writer writer;
 uint16_t song, destination;
 uint32_t writer_phase; /* uses former tail padding:0 idle,1 clone,2 capture */
} music_session;
void music_session_init(music_session *);
/* Call after exclusive FAT/extent validation. Header validation is not a full
 * payload scrub. Metadata output belongs to caller's stopped-song load path. */
bool music_session_load(music_session *,pool_store *,pool_io,unsigned song,take_metadata out[4]);
/* Same load/publication checks using a catalog already opened by the storage
 * coordinator. Catalog and controls must belong to the same load operation. */
bool music_session_load_opened(music_session *,pool_store *,unsigned song,take_metadata out[4]);
bool music_session_map(const music_session *,unsigned track,uint32_t offset,uint32_t wanted,uint32_t *lba,uint32_t *count);
bool music_session_stash(music_session *,pool_store *,pool_io,unsigned track,unsigned stash,bool stopped);
bool music_session_recall(music_session *,pool_store *,pool_io,unsigned stash,unsigned track,bool stopped);
bool music_session_record(music_session *,pool_store *,pool_io,unsigned track,uint32_t capacity,bool stopped);
bool music_session_append(music_session *,pool_io,const void *,uint32_t blocks);
bool music_session_finish(music_session *,pool_store *,pool_io,const take_metadata *,bool stopped);
bool music_session_abort(music_session *,bool stopped);
bool music_session_host_request(music_session *,bool stopped);
bool music_session_host_grant(music_session *,bool synced,bool closed,bool drained);
bool music_session_host_return(music_session *);
/* Runtime-bound variants: references/audio and full controls publish together.
 * snapshot must describe the proposed state and stay stable for six reads.
 * Metadata/extent outputs publish only after successful durable commit. */
bool music_session_stash_control(music_session *,pool_store *,pool_io,unsigned track,unsigned stash,bool stopped,music_control_snapshot,void *);
bool music_session_recall_control(music_session *,pool_store *,pool_io,unsigned stash,unsigned track,bool stopped,music_control_snapshot,void *,take_metadata *);
bool music_session_finish_control(music_session *,pool_store *,pool_io,const take_metadata *,bool stopped,music_control_snapshot,void *);
/* Clone while stopped, then patch detached copy during recording. Caller supplies
 * existing batch scratch; no allocation or shared-workspace retention. */
bool music_session_edit(music_session *,pool_store *,pool_io,unsigned track,bool stopped);
bool music_session_edit_capacity(music_session *,pool_store *,pool_io,unsigned track,uint32_t audio_capacity,bool stopped);
bool music_session_patch(music_session *,pool_io,uint32_t offset,const uint8_t *,uint32_t blocks,uint8_t scratch[512]);
/* Bounded native flush: patch existing detached audio then append any tail.
 * No sparse holes; a failed write poisons the unpublished take. */
bool music_session_write(music_session *,pool_io,uint32_t audio_offset,const uint8_t *,uint32_t blocks,uint8_t scratch[512]);
/* Incremental preparation: each step copies <=8 sectors using caller scratch.
 * The caller interleaves playback refill; finish verifies CRC before capture.
 * Workspace can be discarded between steps. No new retained allocation. */
bool music_session_edit_prepare(music_session *,pool_store *,pool_io,unsigned track,uint32_t audio_capacity,bool stopped);
bool music_session_edit_step(music_session *,pool_io,void *scratch,uint32_t scratch_blocks);
bool music_session_edit_ready(music_session *,pool_store *,pool_io);
/*427: coordinator-only phases; no extra retained memory. A prepared extent
 * is revoked BEFORE every catalog open/mutation or host handoff. It is never
 * a durable object and may be reclaimed when unclaimed. */
#define MUSIC_PREPARED427 3u
#define MUSIC_NO_RESERVE427 4u
#define MUSIC_AUDITION427 5u
static inline void music_prepared427_revoke(music_session *s){
 if(s&&!s->ownership.writer&&(s->writer_phase==MUSIC_PREPARED427||s->writer_phase==MUSIC_NO_RESERVE427)){
  s->writer.active=false;__atomic_store_n(&s->writer_phase,0,__ATOMIC_RELEASE);
 }
}
static inline bool music_prepared427_claim(music_session *s,unsigned track,uint32_t capacity){
 if(!s||track>=4||s->song>=16||s->tracks[track].count||
    s->writer_phase!=MUSIC_PREPARED427||s->ownership.owner!=SP1_STORE_PERFORMANCE||
    s->ownership.readers||s->ownership.writer||!s->writer.active||s->writer.failed||
    (s->writer.fault&0x80000000u)||s->writer.written!=TAKE_HEADER_BLOCKS||capacity>UINT32_MAX-TAKE_HEADER_BLOCKS||
    s->writer.capacity!=capacity+TAKE_HEADER_BLOCKS)return false;
 if(!sp1_writer_acquire(&s->ownership))return false;
 s->destination=s->song*4+track;
 __atomic_store_n(&s->writer_phase,2,__ATOMIC_RELEASE);return true;
}
/* A drained, valid take may be read through its detached extent before its
 * catalog commit. Keep writer ownership: no new recording, abort, host access,
 * allocation or shutdown can discard/reuse this audible pending take. */
bool music_audition427(music_session *,pool_io,unsigned track,const take_metadata *);
#endif
