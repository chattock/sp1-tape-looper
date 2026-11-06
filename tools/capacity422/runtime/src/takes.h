#ifndef TAKES397_H
#define TAKES397_H
#include "engine.h"
#include "take.h"
/* Storage worker only. No new buffers in the audio path, catalog v4 unchanged. */
int takes_view(pool_store *,unsigned lane,uint8_t slots[64]);
/* RAM-only snapshot: bits0..27 are four7-bit ordinals, bits28..31 song.
 *0=New,1..65=distinct saved/live version,127=unknown. UINT32_MAX initially.
 * UI must hide during save/record/selection/song handoffs and faults. */
uint32_t music_engine_takes404_snapshot(void);
int music_engine_take404(unsigned song,unsigned track);
int takes_archive(pool_store *,unsigned lane,const take_metadata *); /* -1 failure */
bool takes_delete(pool_store *,unsigned lane);
int music_engine_cycle(music_runtime_retained *,music_runtime_workspace *,unsigned,int,unsigned *);
bool music_engine_take_ordinals(unsigned,uint32_t[4]);
/* FN display only: total distinct takes, including the live version, never New. */
bool music_engine_take_preview442(unsigned,uint32_t[4]);
bool music_engine_take_lanes517(unsigned,uint32_t[4],uint32_t[4]);
#endif

unsigned music_engine_feedback429(music_runtime_workspace *,unsigned,unsigned,unsigned);

unsigned music_engine_empty_saved430(unsigned);
