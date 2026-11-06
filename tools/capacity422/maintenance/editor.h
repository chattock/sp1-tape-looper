#ifndef CAT422_EDITOR_H
#define CAT422_EDITOR_H
#include "discover.h"
#include "publication.h"
typedef struct {discover422 discovery;publication422 publication;uint32_t changes,done;unsigned error,phase;} editor422;
/* Host must have ejected. Gate remains held until caller clears it after this
 * returns true. All WAVs and final FAT are validated before native writes. */
bool editor422_verify(cat422_workspace*,pool_io,uint64_t expected_generation);
bool editor422_commit(editor422*,drive422*,cat422_workspace*);
/* Explicit recovery: verify the journaled target tree, every referenced payload
 * CRC and controls before replaying a torn target header. No new edit plan. */
bool editor422_discard(editor422*,drive422*,cat422_workspace*);
bool editor422_recover(editor422*,drive422*,cat422_workspace*);
#endif
