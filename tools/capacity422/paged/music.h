#ifndef SP1_MUSIC422_H
#define SP1_MUSIC422_H
#include "io.h"
#include "workspace.h"
bool cat422_take_begin(cat422_workspace*,cat422_device*,uint32_t capacity,pool_writer*);
bool cat426_take_begin_hint(cat422_workspace*,cat422_device*,uint32_t capacity,uint32_t hint,pool_writer*);
bool cat422_take_stage_object(cat422_workspace*,cat422_device*,pool_writer*,const take_metadata*,uint32_t*id);
bool cat422_take_stage(cat422_workspace*,cat422_device*,pool_writer*,unsigned lane,const take_metadata*);
bool cat422_take_objects(cat422_workspace*,cat422_device*,uint32_t audio,uint32_t annotation,take_metadata*,pool_object*);
bool cat422_take_load(cat422_workspace*,cat422_device*,unsigned lane,take_metadata*,pool_object*);
bool cat422_take_annotate(cat422_workspace*,cat422_device*,unsigned lane,const take_metadata*);
bool cat422_controls_load(cat422_workspace*,cat422_device*);
bool cat422_controls_stage(cat422_workspace*,cat422_device*);
#endif
