#ifndef CAT422_DISCOVER_H
#define CAT422_DISCOVER_H
#include "drive.h"
#include "import.h"
/* Low8 directory_valid bits belong to the existing read cache. Bit8 is a
 * preflight capability flag, retained across read-ahead cache replacement. */
#define DISCOVER510_GENERAL_WORK (1u<<8)
typedef struct {uint32_t cluster,role,lane;} discover422_dir;
typedef struct {drive422*drive;cat422_manifest files;pg422_io seen;discover422_dir dirs[256];uint32_t nd;uint8_t sector[512],chain_sector[512];uint32_t chain_page;uint8_t directory_cache[8][512];uint32_t directory_lba[8],directory_valid,directory_next,directory_hits,directory_misses;unsigned input_error;uint8_t input_name[12];bool chain_valid,ready;} discover422;
/* Validates the entire final FAT before publishing a source-file manifest.
 * Fixed directories + streamed files: no file-count-sized RAM allocation. */
bool discover422_build(discover422*,drive422*);
/* Complete frozen preflight also returns its already-validated change count. */
bool discover512_build(discover422*,drive422*,uint32_t *changes,uint32_t *reservation);
bool discover505_build(discover422*,drive422*,uint32_t *changes);
/*32-byte spool: lane|attr<<8, original descriptor index+1 or0, first cluster,
 * bytes, eleven-byte short filename, reserved byte, duplicate-name hash link. */
typedef struct {discover422*s;uint32_t cluster,bytes,cached_cluster,cached_index;bool cached;} discover422_source;
bool discover422_source_open(discover422*,uint32_t file,discover422_source*,wav_source*);
bool discover422_get(discover422*,uint32_t file,import422_edit*);
int discover422_deleted(discover422*,uint32_t original,import422_edit*);
bool discover422_seen(discover422*,uint32_t original,bool*);
#endif
