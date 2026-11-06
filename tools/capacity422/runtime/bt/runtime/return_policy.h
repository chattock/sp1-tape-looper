#pragma once
#include <stdbool.h>
/* Only an already selected MIDI session may overlap USB audio. No startup,
 * workspace transition, SBC decode or storage exception. */
static inline bool midi_return_allowed(unsigned mode,unsigned phase,bool active,
 bool normal_safe,bool storage_safe){
 return normal_safe || (storage_safe && mode==5 && phase==4 && active);
}
