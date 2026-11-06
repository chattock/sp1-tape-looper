#ifndef SP1_NATIVE593_H
#define SP1_NATIVE593_H
#include "../library-writeback/wav.h"
/* SP1N593 v1 staging container; never a WAV or a directly published object.
 * Header/payload use existing offline scratch. No static storage or audio code. */
typedef struct {uint32_t frames,blocks,crc,lane,action,ordinal;uint64_t identity,epoch,generation;} native593_info;
bool native593_probe(wav_source,native593_info*,uint8_t header[512]);
bool native593_blocks(const uint8_t*,unsigned);
bool native593_inspect(wav_source,native593_info*,uint8_t*,unsigned);
#endif
