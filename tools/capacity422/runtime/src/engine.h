#ifndef MUSIC_ENGINE367_H
#define MUSIC_ENGINE367_H
#include "backend.h"
/* Serialized storage-thread operations. stopped_drained must mean both audio
 * and asynchronous I/O acknowledged. No pointers into workspace survive calls.
 * Caller applies loaded[]/controls only after success and before audio resumes. */
/* Read-only whole-library geometry admission, once before performance. */
bool music_engine_preflight(music_runtime_retained *,music_runtime_workspace *,bool stopped_drained);
bool music_engine_load(music_runtime_retained *,music_runtime_workspace *,unsigned song,bool stopped_drained);
bool music_engine_stash(music_runtime_retained *,music_runtime_workspace *,unsigned track,unsigned stash,bool stopped_drained);
bool music_engine_recall(music_runtime_retained *,music_runtime_workspace *,unsigned stash,unsigned track,bool stopped_drained);
bool music_engine_begin(music_runtime_retained *,music_runtime_workspace *,unsigned track,uint32_t capacity,bool edit,bool stopped_drained);
/* Starts a bounded/background COW preparation, not capture. Use session step
 * and ready before accepting recorder input; cancel remains possible. */
bool music_engine_prepare_edit(music_runtime_retained *,music_runtime_workspace *,unsigned track,uint32_t capacity,bool stopped_drained);
/* Preserve an occupied take, then reserve a fresh sequential replacement.
 * Capture must write a prefix starting at offset0. No old payload is copied;
 * caller supplies valid encoded silence if final loop geometry exceeds the
 * captured prefix. Old selection/mapping survive cancellation and failures. */
bool music_engine_prepare_replace(music_runtime_retained *,music_runtime_workspace *,unsigned track,uint32_t capacity,bool stopped_drained);
bool music_engine_finish(music_runtime_retained *,music_runtime_workspace *,const take_metadata *,bool stopped_drained);
/* ref0..63 song tracks;64..127 stash. Track deletion must target loaded song. */
bool music_engine_drop(music_runtime_retained *,music_runtime_workspace *,unsigned ref,bool stopped_drained);
/* Caller supplies a stable native snapshot. Only existing references or removals
 * may be saved here; new audio can only be published by record finalization. */
bool music_engine_save(music_runtime_retained *,music_runtime_workspace *,music_control_snapshot,void *,bool);
bool music_engine_finish_snapshot(music_runtime_retained *,music_runtime_workspace *,const take_metadata *,music_control_snapshot,void *,bool);

#endif
