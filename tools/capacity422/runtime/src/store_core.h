#ifndef SP1_STORE_CORE_H
#define SP1_STORE_CORE_H
#include <stdbool.h>
#include <stdint.h>
/* Experimental runtime contract. Not a disk serialization format.
 * A maintenance validator admits contiguous native-codec payloads. No FAT,
 * allocation, locks, I/O, or unbounded scan is performed by playback mapping. */
typedef struct { uint32_t first, count; } sp1_extent;
typedef struct { uint32_t device_blocks, first, count; } sp1_volume;
typedef enum { SP1_STORE_INVALID, SP1_STORE_PERFORMANCE, SP1_STORE_DRAINING,
               SP1_STORE_HOST, SP1_STORE_VALIDATING } sp1_owner;
typedef struct { sp1_owner owner; uint32_t readers; bool writer; } sp1_ownership;
bool sp1_extent_valid(sp1_volume v, sp1_extent e);
bool sp1_extent_map(sp1_extent e, uint32_t offset, uint32_t requested,
                    uint32_t *physical, uint32_t *count);
/* No I/O/allocation. Separates native loop wrap from reserved payload length;
 * reverse returns ascending physical range for reverse consumption. */
bool sp1_extent_window(sp1_extent,uint32_t loop_blocks,uint32_t offset,
                       uint32_t wanted,bool reverse,uint32_t *lba,uint32_t *count);
/* Validate all admitted reservations together, not just file lengths. */
bool sp1_extents_disjoint(sp1_volume v, const sp1_extent *e, uint32_t n);
/* Caller supplies parsed/validated FAT32 geometry and an I/O callback. Reads
 * every reserved cluster, rejects fragmentation, bad/free/reserved links and
 * unexpected tail allocation. Run only while firmware exclusively owns storage.
 * Does not prove FAT copies, directory size, hashes, or cross-file ownership. */
typedef bool (*sp1_fat_read)(void *context, uint32_t cluster, uint32_t *value);
bool sp1_fat_contiguous(sp1_volume data, uint32_t sectors_per_cluster,
                       uint32_t clusters, uint32_t first_cluster,
                       uint32_t reservation_clusters, sp1_fat_read read,
                       void *context, sp1_extent *out);
void sp1_owner_init(sp1_ownership *s);
bool sp1_owner_validated(sp1_ownership *s, bool valid);
bool sp1_reader_acquire(sp1_ownership *s);
bool sp1_reader_release(sp1_ownership *s);
bool sp1_writer_acquire(sp1_ownership *s);
bool sp1_writer_release(sp1_ownership *s);
bool sp1_host_request(sp1_ownership *s, bool stopped);
bool sp1_host_grant(sp1_ownership *s, bool flushed, bool filesystem_closed,
                    bool callbacks_drained);
bool sp1_host_return(sp1_ownership *s);
#endif
