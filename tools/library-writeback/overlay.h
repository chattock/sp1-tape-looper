#ifndef MUSIC_OVERLAY_H
#define MUSIC_OVERLAY_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
/* Host-tested staging primitive. NOT linked into410 and NOT an allocator.
 * Caller supplies an exclusive, durably reserved extent disjoint from every
 * native object/catalog/settings extent and keeps ownership until recovery. */
#define OV_ENTRIES 39u
#define OV_NONE UINT32_MAX
#define OV_WRITE_BATCH 2u
typedef bool (*ov_read)(void*,uint32_t,void*,uint32_t);
typedef bool (*ov_write)(void*,uint32_t,const void*,uint32_t);
typedef bool (*ov_sync)(void*);
typedef struct {void*ctx;ov_read read;ov_write write;ov_sync sync;uint32_t first,blocks,device_blocks,buckets,virtual_sectors;uint8_t session[32];} ov_io;
typedef struct {ov_io io;uint32_t next,limit;bool ready;uint8_t present[2048],occupied[512];uint8_t pages[2][512] __attribute__((aligned(4)));uint8_t verify[512] __attribute__((aligned(4)));} overlay;
/* Explicit creation only. Never a fallback for a failed open. */
bool overlay_create(overlay*,ov_io);
bool overlay_open(overlay*,ov_io);
/* Returns false on storage/checksum failure; found=false means use base view. */
bool overlay_get(overlay*,uint32_t virtual_lba,void*sector,bool*found);
/* Frozen/read-only span: checked maps and payload CRCs; missing bits use base.
 * count2..16, output count*512 bytes; no alias with overlay scratch. */
bool overlay_get_many(overlay*,uint32_t virtual_lba,void*sectors,uint32_t count,uint32_t*found_mask);
/* Append payload, flush, publish inactive bucket checkpoint, flush/readback.
 * Any failed write poisons this session until reopen. Original media untouched. */
bool overlay_put(overlay*,uint32_t virtual_lba,const void*sector);
/* Consecutive sectors with distinct buckets. Payloads are durable and verified
 * before any map is published. Each recovered sector is old or complete new;
 * only a successful return acknowledges the entire batch. Disk format unchanged. */
bool overlay_put_many(overlay*,uint32_t virtual_lba,const void*sectors,uint32_t count);
/*16-sector host batch using caller-owned16KiB scratch after admission.
 * Same payload-before-map barriers and exact readback; no persistent RAM added. */
bool overlay_put_span(overlay*,uint32_t virtual_lba,const void*sectors,uint32_t count,void*scratch,uint32_t scratch_bytes);
/*602: compare and prepare once, packing only changed sectors into one durable
 * transaction. base reads the immutable virtual view, never this overlay.
 * Same16KiB exclusive scratch, unchanged disk format and overlay size. */
bool overlay_update_span(overlay*,uint32_t,const void*,uint32_t,void*,uint32_t,ov_read base,void*base_ctx);
/* Optional immutable, compact index in existing admission workspace. Build:
 *1 ready,0 bounded fallback,-1 checked I/O failure. No overlay struct growth. */
void *overlay509_workspace(size_t*bytes);
void overlay509_invalidate(void);
int overlay509_build(overlay*);
void *overlay513_tail(uint32_t bytes);
uint32_t overlay509_count(overlay*);
typedef bool (*ov_visit)(void*,uint32_t virtual_lba);
bool overlay_visit(overlay*,ov_visit,void*);
#endif
