#include "runtime.h"
bool music_runtime_map(const music_session *m,uint32_t virtual_lba,uint32_t blocks,
                       uint32_t legacy_first,uint32_t stride,uint32_t *physical,uint32_t *mapped){
 if(!m||!stride||virtual_lba<legacy_first||m->song>=16)return false;
 uint32_t relative=virtual_lba-legacy_first,slot=relative/stride,offset=relative%stride;
 if(slot/4!=m->song||blocks>stride-offset)return false;
 return music_session_map(m,slot%4,offset,blocks,physical,mapped);
}
bool music_runtime_window(const music_session *m,unsigned track,const take_metadata *meta,
                          uint32_t offset,uint32_t wanted,bool reverse,uint32_t *physical,uint32_t *mapped){
 if(!m||!meta||track>=4||m->ownership.owner!=SP1_STORE_PERFORMANCE||
    !meta->frames_per_block||!meta->frames||meta->audio_blocks!=m->tracks[track].count)return false;
 uint32_t wrap=meta->loop_blocks;
 if(!wrap)wrap=1+(meta->frames-1)/meta->frames_per_block;
 return sp1_extent_window(m->tracks[track],wrap,offset,wanted,reverse,physical,mapped);
}
