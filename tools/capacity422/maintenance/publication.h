#ifndef CAT422_PUBLICATION_H
#define CAT422_PUBLICATION_H
#include "arena.h"
/* COW root journal inside WORK prefix. Caller freezes/validates the entire edit
 * plan first; one publication per session. No implicit retry, unlock or format. */
typedef struct {arena422*arena;uint32_t state,target,original_crc,target_crc;unsigned active;uint64_t sequence;bool ready;uint8_t sector[512];} publication422;
bool publication422_create(publication422*,arena422*);
/*0=refused,1=source still authoritative/target may be torn,2=exact target header
 * present. Caller MUST verify target tree/payloads before session completion. */
unsigned publication422_open(publication422*,arena422*);
pool_io publication422_target_io(publication422*);
pool_io publication422_io(publication422*);
/* Explicit replay of the durably journaled root; all other roots must retain
 * original bytes. Does not accept a fresh target or release the boot gate. */
bool publication422_replay(publication422*);
#endif
