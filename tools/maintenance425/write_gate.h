#ifndef WRITE_GATE_H
#define WRITE_GATE_H
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
/* One-to-sixteen-sector tail staging. No DMA or card call until exact commit and CRC match.
 * After an error the caller must latch the session. No retry inside firmware. */
struct write_gate {uint8_t bytes[8192];uint32_t seq,lba,crc,blocks;unsigned filled;bool active;};
static bool gate_begin(struct write_gate *g,uint32_t seq,uint32_t lba,uint32_t crc,uint32_t blocks,bool admitted){
 if(!admitted||g->active||!seq||!blocks||blocks>16||!((lba==0&&blocks==1)||(lba>=5509120u&&lba<7733248u&&blocks<=7733248u-lba)))return false;
 g->seq=seq;g->lba=lba;g->crc=crc;g->blocks=blocks;g->filled=0;g->active=true;return true;
}
static bool gate_data(struct write_gate *g,uint32_t seq,uint32_t offset,const uint8_t *b,unsigned n){
 if(!g->active||seq!=g->seq||offset!=g->filled||!n||g->filled+n>g->blocks*512)return false;
 memcpy(g->bytes+g->filled,b,n);g->filled+=n;return true;
}
static bool gate_ready(struct write_gate *g,uint32_t seq,uint32_t crc){return g->active&&g->seq==seq&&g->filled==g->blocks*512&&g->crc==crc;}
#endif
