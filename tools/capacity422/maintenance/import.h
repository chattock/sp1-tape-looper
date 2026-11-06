#ifndef CAT422_IMPORT_H
#define CAT422_IMPORT_H
#include "../paged/music.h"
#include "../../library-writeback/wav.h"
enum {IMPORT422_DELETE=1,IMPORT422_REPLACE,IMPORT422_ADD};
typedef struct {uint32_t action,lane,archive,audio,annotation;wav_source source;} import422_edit;
/* Offline maintenance coordinator only, under persistent gate and immutable
 * validated final FAT/edit manifest. Begin/load controls before calling. No
 * publication occurs here: caller journals the target root before commit. */
bool import422_stage(cat422_workspace*,cat422_device*,const import422_edit*);
/* Optional sequential search hint, never authority to overwrite an extent. */
/* Internal offline batch reservation: editor proves the whole tail free against
 * the transaction and BOTH immutable checkpoints before any staged writes. */
/*593: distinguish successfully-read malformed native input from media I/O failure. */
bool import593_stage(cat422_workspace*,cat422_device*,const import422_edit*,uint32_t*,uint32_t,unsigned*input_error);
bool import512_stage(cat422_workspace*,cat422_device*,const import422_edit*,uint32_t *cursor,uint32_t reserved_end);
bool import504_stage(cat422_workspace*,cat422_device*,const import422_edit*,uint32_t *hint);
/* Normalize all current tracks and stage one new controls object. Does NOT
 * commit or release the gate. Caller must finish its complete edit plan first. */
bool import422_controls(cat422_workspace*,cat422_device*);
#endif
