#ifndef SP1_ALLOCATE422_H
#define SP1_ALLOCATE422_H
#include "catalog.h"
/* Call only with exclusive storage ownership. Pins both valid checkpoints
 * plus staged catalog. Caller retains reservation through writer completion.
 * Extra extent reserves space for the following control/annotation commit. */
bool cat422_find_room(pg422*,pg422 *checkpoint_workspace,uint32_t blocks,uint32_t alignment,uint32_t pool_lba,uint32_t extra_first,uint32_t extra_blocks,uint32_t *first);
/* A placement hint is never allocation authority: check both checkpoints and
 * staged extents, then wrap to the full first-fit search if the tail is full. */
bool cat426_find_room_hint(pg422*,pg422*,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t*);
#endif
