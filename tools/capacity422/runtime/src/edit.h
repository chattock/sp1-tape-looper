#ifndef SP1_TAKE_EDIT_H
#define SP1_TAKE_EDIT_H
#include "take.h"
/* Explicit experimental COW editing backend. Clone only while stopped, before
 * admitting musical punch-in. Never overwrite a committed or stashed object.
 * Patch data and512-byte scratch must not overlap.
 * Patch needs an extra old-data read; hardware timing is NOT qualified yet. */
bool take_edit_begin(pool_store *,uint16_t source,pool_writer *);
/* Optional larger reservation for a growing replacement; zero preserves size. */
bool take_edit_begin_capacity(pool_store *,uint16_t source,uint32_t audio_capacity,pool_writer *);
bool take_edit_patch(pool_io,pool_writer *,uint32_t audio_offset,const uint8_t *,uint32_t blocks,uint8_t scratch[512]);
#endif
