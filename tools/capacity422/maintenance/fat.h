#ifndef CAT422_EDIT_FAT_H
#define CAT422_EDIT_FAT_H
#include <stdbool.h>
#include <stdint.h>
typedef bool (*fat_read)(void*,uint32_t,void*,uint32_t);
typedef struct {void*ctx;fat_read read;uint32_t sectors,fat_start,fat_sectors,data_start,clusters,unavailable_from;uint8_t sectors_per_cluster;} fat_view;
typedef enum {FAT_OK,FAT_IO,FAT_LAYOUT,FAT_CROSSLINK,FAT_ORPHAN,FAT_LIMIT,FAT_VISITOR} fat_result;
typedef struct {uint32_t cluster,bytes,parent;uint8_t attributes,depth;char name[11];} fat_file;
/* Visitor may COLLECT a tentative manifest only. No publishing or device writes
 * until the entire walk succeeds, including final orphan/mirror checks. */
typedef bool (*fat_visit)(void*,const fat_file*);
/* External ownership bitmap is a disjoint maintenance reservation. One cached
 * sector replaces an allocation proportional to host volume size. */
typedef struct {void*ctx;bool(*read)(void*,uint32_t,void*);bool(*write)(void*,uint32_t,const void*);bool(*sync)(void*);uint32_t sectors;} fat422_bitmap;
/* Fixed depth8, geometric scan bound; no65536-entry ceiling. No publishing or
 * audio/index writes are allowed from visitor callbacks. */
fat_result fat422_validate(fat_view,fat422_bitmap,fat_visit,void*);
#endif
