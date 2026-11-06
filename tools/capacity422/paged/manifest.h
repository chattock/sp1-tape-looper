#ifndef CAT422_MANIFEST_H
#define CAT422_MANIFEST_H
#include "store.h"
/* Disposable maintenance directory spool in a caller-owned DISJOINT reservation.
 * Not the music catalog. Header is published only after all data is flushed and
 * read back. No allocation, no 128-file ceiling, no writes to the source index.
 * Caller supplies a fresh reservation under the persistent maintenance gate. */
typedef struct {pg422_io io;uint64_t generation;uint32_t count,used;bool ready,building;uint8_t sector[512];} cat422_manifest;
bool cat422_manifest_begin(cat422_manifest*,pg422_io,uint64_t generation);
bool cat422_manifest_append(cat422_manifest*,const uint8_t record[32]);
bool cat422_manifest_peek(cat422_manifest*,uint32_t id,uint8_t record[32]);
bool cat422_manifest_patch(cat422_manifest*,uint32_t id,const uint8_t record[32]);
bool cat422_manifest_finish(cat422_manifest*);
bool cat422_manifest_open(cat422_manifest*,pg422_io,uint64_t generation);
bool cat422_manifest_get(cat422_manifest*,uint32_t id,uint8_t record[32]);
#endif
