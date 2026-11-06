#include "directory.h"
#include "enumerate.h"
#include <limits.h>
static void put(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=n>>(i*8);}
typedef struct {cat422_workspace*w;cat422_device*d;cat422_manifest*out;} build_ctx;
static bool emit(void*c,const cat422_entry*e){
 build_ctx*x=c;take_metadata m;pool_object a;uint8_t b[32];
 if(!cat422_take_objects(x->w,x->d,e->audio,e->annotation,&m,&a))return false;
 uint64_t frames=(uint64_t)m.audio_blocks*m.frames_per_block;if(frames>m.frames)frames=m.frames;
 if(!frames||frames>(UINT32_MAX-44u)/4)return false;
 put(b,e->lane|((uint32_t)e->current<<16));put(b+4,e->ordinal);put(b+8,e->audio);put(b+12,e->annotation);
 put(b+16,a.first);put(b+20,a.blocks);put(b+24,frames);if(e->archive>=PG422_LIMIT||m.frames_per_block>511)return false;put(b+28,m.frames_per_block|(e->archive<<9));
 return cat422_manifest_append(x->out,b);
}
bool cat422_directory_build(cat422_workspace*w,cat422_device*d,cat422_manifest*out,pg422_io spool){
 if(!w||!d||!out||!w->index.ready||w->index.transaction||!cat422_manifest_begin(out,spool,w->index.generation))return false;
 build_ctx ctx={w,d,out};
 for(unsigned lane=0;lane<65;lane++){uint32_t count;if(!cat422_enumerate(&w->index,lane,emit,&ctx,&count)){out->building=false;return false;}}
 return cat422_manifest_finish(out);
}
