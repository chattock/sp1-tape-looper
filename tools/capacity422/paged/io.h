#ifndef SP1_PAGED_IO422_H
#define SP1_PAGED_IO422_H
#include "allocate.h"
#include "../../commit421/runtime/src/pool.h"
/* Context lives only in the exclusive coordinator workspace. FAT admission
 * must establish the full contiguous index extent before constructing this. */
typedef struct {pool_io raw;uint32_t index_first,index_blocks;} cat422_device;
bool cat422_device_open(pg422*,cat422_device*);
bool cat422_banks_open(pg422*,pg422*,cat422_device*,pool_io);
/* Requires transport idle and exclusive ownership for the whole operation.
 * Source bank remains authoritative until the verified higher generation. */
bool cat422_banks_compact(pg422*,pg422*,cat422_device*,bool transport_idle);
/* Reserve before starting capture; use existing32-byte retained writer. ID is
 * assigned at publication, so the writer does not retain a catalog pointer. */
bool cat422_writer_prepare(pg422*,pg422*,cat422_device*,uint32_t capacity,pool_writer*);
bool cat426_writer_prepare_hint(pg422*,pg422*,cat422_device*,uint32_t capacity,uint32_t hint,pool_writer*);
/* Caller appends using pool_writer_append with the admitted raw pool mapping.
 * Finish is staged with controls in one index transaction; no auto commit. */
bool cat422_writer_object(pg422*,cat422_device*,pool_writer*,uint32_t tag,uint32_t*id);
bool cat422_writer_stage(pg422*,cat422_device*,pool_writer*,unsigned lane,uint32_t tag);
bool cat422_read_audio(pg422*,cat422_device*,unsigned lane,uint32_t offset,void*,uint32_t blocks);
#endif
