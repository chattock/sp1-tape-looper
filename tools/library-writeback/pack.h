#ifndef WRITEBACK_PACK_H
#define WRITEBACK_PACK_H
#include <stdint.h>
typedef struct {int error[2];unsigned shift;} wb_encoder;
/* One280-frame stereo engine block -> existing512-byte P14S format.
 * Bit-exact to the maintained transfer importer; no audio runtime linkage. */
void wb_pack(wb_encoder*,const int16_t stereo[560],uint8_t out[512]);
#endif
