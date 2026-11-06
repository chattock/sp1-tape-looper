#include "geometry.h"
#include <limits.h>
bool music_geometry_load(uint32_t fpb,uint32_t song,uint32_t blocks,
 uint32_t start,uint32_t exact,uint32_t exact_start,bool valid,
 uint32_t capacity,music_geometry *out){
 if(!out||(fpb!=280&&fpb!=496)||!blocks||blocks>capacity)return false;
 uint64_t frames=(uint64_t)blocks*fpb,anchor=(uint64_t)start*fpb;
 if(song&&song%fpb){uint64_t multiple=(frames+song/2)/song;if(!multiple)multiple=1;frames=multiple*song;}
 /*419: baked loops may end anywhere in their final physical block. Accept
  * ceiling length as well as legacy nearest; exact frames and capacity stay
  * checked, and no musical samples are added or removed. */
 /*417: legacy controls use floor, live capture uses nearest block. Both
  * must retain the checksum-validated sample anchor; reject unrelated blocks. */
 if(valid&&exact&&(((uint64_t)exact+fpb/2)/fpb==blocks||((uint64_t)exact+fpb-1)/fpb==blocks)&&(exact_start/fpb==start||((uint64_t)exact_start+fpb/2)/fpb==start)){frames=exact;anchor=exact_start;}
 if(!frames||frames>UINT32_MAX||anchor>UINT32_MAX)return false;
 *out=(music_geometry){(uint32_t)frames,(uint32_t)anchor};return true;
}
