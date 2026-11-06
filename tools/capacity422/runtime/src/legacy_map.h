#ifndef LEGACY_MAP373_H
#define LEGACY_MAP373_H
#include "runtime.h"
/* Storage-thread only, at boot or after both handoff acknowledgements. */
static inline bool legacy_map_bind(music_session *m, bool admitted, unsigned song,
 uint32_t capacity, uint32_t first, uint32_t stride){
 m->ownership.owner=SP1_STORE_INVALID;
 if(!admitted || song>=16 || !stride || first>=capacity ||
    stride>(capacity-first)/64u) return false;
 for(unsigned i=0;i<4;i++)m->tracks[i]=(sp1_extent){first+(song*4+i)*stride,stride};
 m->song=song;m->ownership.owner=SP1_STORE_PERFORMANCE;return true;
}
static inline bool legacy_map_read(const music_session *m,uint32_t b,uint32_t n,
 uint32_t first,uint32_t stride,uint32_t *physical){
 uint32_t count=0;
 return n && music_runtime_map(m,b,n,first,stride,physical,&count) && count==n;
}
#endif
