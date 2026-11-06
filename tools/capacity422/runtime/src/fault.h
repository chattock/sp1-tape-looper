#ifndef MUSIC_FAULT400_H
#define MUSIC_FAULT400_H
#include "music_workspace.h"
#define MUSIC_FAULT_BIT 0x80000000u
enum { MUSIC_PREP_LOAD=9, MUSIC_PREP_PRESERVE=10, MUSIC_PREP_RESERVE=11,
       MUSIC_PREP_COPY=12, MUSIC_PREP_VERIFY=13 };
/* A failed detached writer is not an ordinary capacity/busy refusal. Some
 * validation failures have no I/O callback to latch the diagnostic bit. */
static inline bool music_fault_pending(const music_runtime_retained *r){
 return r->session.writer.failed || (r->session.writer.fault&MUSIC_FAULT_BIT)!=0 ||
        r->session.ownership.owner==SP1_STORE_INVALID;
}
/* Low byte: transaction step. Bits16..23: I/O failure. First error wins. */
static inline void music_fault_step(music_runtime_retained *r,unsigned step){
 if(!(r->session.writer.fault&MUSIC_FAULT_BIT))r->session.writer.fault=step;
}
static inline void music_fault_fail(music_runtime_retained *r,unsigned io){
 if(!(r->session.writer.fault&MUSIC_FAULT_BIT))r->session.writer.fault|=MUSIC_FAULT_BIT|(io<<16);
}
/*415 uses previously unused bits: reason24..30, reference8..13.
 * Preserve the first latched I/O failure. No additional retained RAM. */
static inline void music_fault_context(music_runtime_retained *r,unsigned ref,unsigned reason){
 if(!(r->session.writer.fault&MUSIC_FAULT_BIT)&&ref<64&&reason>0&&reason<128)
  r->session.writer.fault=(r->session.writer.fault&0x00ff00ffu)|(ref<<8)|(reason<<24);
}
#endif
