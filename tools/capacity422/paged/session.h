#ifndef SP1_SESSION422_H
#define SP1_SESSION422_H
#include "music.h"
#include "../runtime/src/session.h"
/* Requires a validated open index in this exclusive workspace. No media ownership handoff. */
bool cat439_session_load_open(music_session*,cat422_workspace*,cat422_device*,unsigned song);
bool cat422_session_load(music_session*,cat422_workspace*,cat422_device*,unsigned song);
bool cat422_session_prepare(music_session*,cat422_workspace*,cat422_device*,unsigned track,uint32_t capacity,bool clone);
bool cat422_session_edit_ready(music_session*,cat422_workspace*,cat422_device*);
bool cat422_session_finish(music_session*,cat422_workspace*,cat422_device*,const take_metadata*);
/* Storage ownership barrier required. Positive selects New, negative cycles
 * older, zero deletes the live version. Controls must be freshly loaded. */
bool cat422_session_select(music_session*,cat422_workspace*,cat422_device*,unsigned track,int direction);
bool cat440_session_select_open(music_session*,cat422_workspace*,cat422_device*,unsigned track,uint32_t selected,uint32_t ordinal,uint64_t generation);
bool cat422_session_preflight(music_session*,cat422_workspace*,cat422_device*);
bool cat422_session_save(music_session*,cat422_workspace*,cat422_device*);
bool cat427_session_warm(music_session*,cat422_workspace*,cat422_device*,uint32_t capacity);
#endif
