#ifndef SP1_REFILL466_H
#define SP1_REFILL466_H
#include <stdint.h>
#include <stdbool.h>
/* c is virtual playback phase; source is physical source position. Scissors
 * maps across a removed range, so its source cannot be clamped to virtual win.
 * Every clamp is monotone: never turn a smaller admitted burst into a larger one. */
static inline uint32_t refill466_tile(uint32_t n,uint32_t c,uint32_t win,uint32_t period,uint32_t base,uint32_t source,bool reverse,bool scissors){
 if(!n||!win)return 0;
 if(scissors)return n; /* glue_span clips the actual splice/cycle boundary. */
 uint64_t low=(uint64_t)(c/win)*period+base,high=low+win;
 if(source<low||source>=high)return 0;
 uint64_t room=reverse?(uint64_t)source-low+1:high-source;
 return room<n?(uint32_t)room:n;
}
/* Last gate before reading/zeroing/decoding a scratch buffer. Independent of
 * effect geometry and safe even if an effect changes during the calculation. */
static inline bool refill466_valid(uint32_t n,uint32_t capacity,uint32_t budget,uint32_t source,uint32_t blocks,bool reverse){
 return n&&n<=capacity&&n<=budget&&source<blocks&&
  (reverse?n<=source+1u:n<=blocks-source);
}
#endif
