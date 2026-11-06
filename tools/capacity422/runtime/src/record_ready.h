/* Recording338: permission to replace a take is not an audio-engine arm. */
#ifndef RECORD_READY_H
#define RECORD_READY_H
#include <stdint.h>
struct record_ready { uint32_t until; uint8_t track, slot, mode, released; };
static inline void rr_clear(struct record_ready *r) { r->track=0; }
static inline void rr_prepare(struct record_ready *r, unsigned track, unsigned slot, unsigned mode, uint32_t now) {
 r->until=now+10000u; r->slot=slot; r->mode=mode; r->released=0; r->track=track+1u;
}
static inline void rr_check(struct record_ready *r,unsigned slot,unsigned mode,uint32_t now,int blocked) {
 if(r->track && (blocked||r->slot!=slot||r->mode!=mode||(int32_t)(now-r->until)>=0))rr_clear(r);
}
static inline int rr_confirm(const struct record_ready *r,unsigned track) { return r->track==track+1u && r->released; }
#endif
