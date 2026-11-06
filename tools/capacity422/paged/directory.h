#ifndef CAT422_DIRECTORY_H
#define CAT422_DIRECTORY_H
#include "manifest.h"
#include "music.h"
/* Fixed32-byte WAV descriptor, little endian words:
 * lane | current<<16, ordinal, audioID, annotationID,
 * absolute audio LBA, encoded blocks, PCM frames, framesPerBlock(low9) | stableArchiveID<<9.
 * Archive ID0 identifies an unsaved live version. Ordinals are presentation
 * only and MUST NOT be used as mutable catalog IDs.
 * Source index and payloads are read-only. Destination spool must be disjoint
 * and protected by the maintenance gate. No namespace mutation here. */
bool cat422_directory_build(cat422_workspace*,cat422_device*,cat422_manifest*,pg422_io spool);
#endif
