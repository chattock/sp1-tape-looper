#ifndef CAT422_VIEW_H
#define CAT422_VIEW_H
#include "wav.h"
/* Read-only virtual FAT view. Both manifests require disjoint, explicitly owned
 * maintenance reservations. Host writeback is a separate transaction layer. */
typedef struct {cat422_manifest *files,*nodes;cat422_wav wav;pool_io raw;uint32_t fat,data,sectors,clusters,used,unavailable_from;uint8_t fat_cache[4][512];uint32_t fat_cache_page[4],fat_cache_valid,fat_cache_next;uint8_t owner_node[32];bool owner_valid,ready,writable;} cat422_view;
bool cat422_view_build(cat422_view*,cat422_manifest *files,cat422_manifest *nodes,pg422_io node_spool,pool_io raw);
bool cat422_view_open(cat422_view*,cat422_manifest *files,cat422_manifest *nodes,pool_io raw);
bool cat422_view_read(cat422_view*,uint32_t lba,uint8_t*,uint32_t sectors);
#endif
