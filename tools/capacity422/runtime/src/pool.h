#ifndef SP1_POOL_H
#define SP1_POOL_H
#include <stdbool.h>
#include <stdint.h>
#define SP1_POOL_OBJECTS 128u
#define SP1_POOL_REFS 128u /*64 song tracks,64 stash slots*/
#define SP1_POOL_CATALOG_BYTES 4096u
#define SP1_POOL_NONE UINT16_MAX
/* Experimental v3 catalog (reads v2), explicit take format, explicit LE encoding in pool.c. */
typedef struct {uint32_t first, blocks, crc, tag;} pool_object;
typedef struct {
 uint8_t lane[64],rank[64]; /* v4 stash ownership/order; zero is unassigned */
 uint64_t generation;
 uint32_t control; /*0 absent, otherwise immutable control object id + 1*/
 pool_object object[SP1_POOL_OBJECTS];
 uint16_t ref[SP1_POOL_REFS];
 uint16_t annotation[SP1_POOL_REFS]; /*0 absent, immutable metadata object id+1*/
} pool_catalog;
typedef bool (*pool_read)(void *,uint32_t,void *,uint32_t);
typedef bool (*pool_write)(void *,uint32_t,const void *,uint32_t);
typedef bool (*pool_sync)(void *);
typedef struct {
 void *context; pool_read read;pool_write write;pool_sync sync;
 uint32_t catalog[2]; uint32_t pool_first,pool_blocks,device_blocks;
} pool_io;
/* Workspace belongs to the storage coordinator; never allocate this on audio
 * stack. Caller serializes against filesystem/USB, streaming and recording. */
typedef struct {
 pool_io io;pool_catalog current, previous;
 bool have_previous,ready;unsigned active;
 uint8_t bytes[SP1_POOL_CATALOG_BYTES];
 uint32_t reserved_first,reserved_blocks,written,crc;
 uint16_t recording_object;bool recording;
} pool_store;
uint32_t pool_crc(uint32_t crc,const void *data,uint32_t length);
uint32_t pool_crc_shift(uint32_t delta,uint64_t zero_bytes);
bool pool_encode(const pool_catalog *,uint8_t bytes[SP1_POOL_CATALOG_BYTES]);
bool pool_decode(const uint8_t bytes[SP1_POOL_CATALOG_BYTES],pool_catalog *);
bool pool_open(pool_store *,pool_io);
/* Explicit image provisioning only. Never called as a mount-error fallback. */
bool pool_initialize(pool_store *,pool_io);
/* Stage reference changes without publishing; finish with control snapshot in
 * the same catalog transaction. On failure discard/reopen the workspace. */
int pool_lane_selected(const pool_catalog *,unsigned);
int pool_lane_browse(const pool_catalog *,unsigned,int);
bool pool_lane_select(pool_store *,unsigned,unsigned);
bool pool_lane_claim(pool_store *,unsigned,unsigned);
bool pool_lane_commit(pool_store *);
bool pool_stage_stash(pool_store *,uint16_t track,uint16_t stash);
bool pool_stage_recall(pool_store *,uint16_t stash,uint16_t track);
bool pool_stash(pool_store *,uint16_t track,uint16_t stash);
bool pool_recall(pool_store *,uint16_t stash,uint16_t track);
bool pool_stage_drop(pool_store *,uint16_t ref);
bool pool_drop(pool_store *,uint16_t ref);
/* Must reserve maximum length before audio starts. Whole sectors, native codec
 * payload plus any immutable per-take metadata supplied by adapter. */
bool pool_record_begin(pool_store *,uint32_t capacity_blocks);
/* Dry-run data allocation plus following control commit, pins both checkpoints. */
/* Refusal codes:64 unavailable,65 busy,66 request,67 descriptor IDs,
 *68 contiguous audio reservation,69 control reserve. Zero means admitted. */
unsigned pool_record_reason(const pool_store *,uint32_t,uint32_t,uint32_t);
bool pool_record_room(const pool_store *,uint32_t blocks,uint32_t alignment,uint32_t following);
/* Align absolute device LBA; control-path allocation only. */
bool pool_record_begin_aligned(pool_store *,uint32_t blocks,uint32_t alignment);
bool pool_record_append(pool_store *,const void *,uint32_t blocks);
bool pool_record_finish(pool_store *,uint16_t destination,uint32_t tag,bool replace);
void pool_record_abort(pool_store *);
bool pool_read_ref(pool_store *,uint16_t ref,uint32_t offset,void *,uint32_t blocks);
bool pool_verify_ref(pool_store *,uint16_t ref);
/* Playback-time writer retains32 bytes instead of the catalog workspace.
 * The coordinator must hold an exclusive writer lease from prepare to finish;
 * no catalog mutation, USB access or other writer is allowed in that interval.
 * Workspace may be reused as the existing storage batch buffer after prepare. */
typedef struct {
 uint64_t generation;
 uint32_t first,capacity,written,crc;
 uint16_t object; bool active,failed;
 uint32_t fault; /*400: occupies the existing tail padding, not extra audio RAM*/
} pool_writer;
_Static_assert(sizeof(pool_writer)==32,"writer diagnostics must not grow RAM");
bool pool_writer_room(const pool_store*,uint32_t,uint32_t,uint32_t,pool_writer*);
bool pool_prepare_writer(pool_store *,uint32_t blocks,pool_writer *);
bool pool_prepare_writer_aligned(pool_store *,uint32_t blocks,uint32_t alignment,pool_writer *);
bool pool_writer_append(pool_io,pool_writer *,const void *,uint32_t blocks);
bool pool_finish_writer(pool_store *,pool_writer *,uint16_t dest,uint32_t tag,bool replace);
/* Storage-coordinator transaction: stage a durable take in RAM, prepare/write
 * a control snapshot, then publish BOTH in one catalog checkpoint. On any
 * error discard workspace and reopen. Never expose staged maps to playback. */
bool pool_stage_writer(pool_store *,pool_writer *,uint16_t dest,uint32_t tag,bool replace);
bool pool_finish_control(pool_store *,pool_writer *,uint32_t tag);
bool pool_read_control(pool_store *,uint32_t offset,void *,uint32_t blocks);



/* Stage a per-reference immutable metadata object before control commit. */
bool pool_stage_annotation(pool_store *,pool_writer *,uint16_t ref,uint32_t tag);
bool pool_read_annotation(pool_store *,uint16_t ref,void *sector);

#endif
