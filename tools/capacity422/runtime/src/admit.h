#ifndef SP1_FAT_ADMIT_H
#define SP1_FAT_ADMIT_H
#include "ff.h"
#include "pool.h"
/* Maintenance-only. Caller owns mounted volume exclusively. This locates and
 * checks the reserved files, both FAT copies and catalog geometry. Unmount and
 * discard FAT caches before handing returned extents to performance mode.
 * Does not format, repair, relocate, validate music or authorize writes. */
bool music_fat_admit(FATFS *,pool_io raw,pool_io *admitted,uint8_t scratch[1024]);
/* Low-stack variant: file object belongs to stopped shared workspace. */
bool music_fat_admit_workspace(FATFS *,pool_io raw,pool_io *admitted,uint8_t scratch[1024],FIL *);
bool music_fat_admit_paged(FATFS *,pool_io,pool_io *,uint8_t[1024],FIL *);
#endif
