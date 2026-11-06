#ifndef STASH_TAP_H
#define STASH_TAP_H
#include <stdbool.h>
#include <stdint.h>
/* 1 reserved; 2 empty-track tap candidate; 3 consumed by any rocker/volume.
 * A candidate NEVER arms while held. Release is the intentional timing point. */
static inline uint8_t stash_tap_consume(uint8_t mark,bool side_active){return mark==2&&side_active?3:mark;}
static inline bool stash_tap_release(uint8_t mark,uint32_t age,bool safe,bool empty){return mark==2&&age>=40&&age<=350&&safe&&empty;}
#endif
