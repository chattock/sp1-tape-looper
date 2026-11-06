#ifndef MUSIC_CONTROLS367_H
#define MUSIC_CONTROLS367_H
#include "take.h"
/* Modify only a stopped private snapshot; publication belongs to the catalog
 * transaction. Existing song grid/length/tempo survive when other tracks exist.
 * Empty-song recall adopts the take's song grid. null take removes one track. */
bool music_controls_track(uint8_t controls[3072],unsigned song,unsigned track,const take_metadata *take);
/* Capture current saved musical settings while retaining audio identity. */
bool music_controls_capture(const uint8_t controls[3072],unsigned song,unsigned track,take_metadata *);
#endif
