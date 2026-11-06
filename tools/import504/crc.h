#ifndef SP1_CRC504_H
#define SP1_CRC504_H
#include <stdint.h>
uint32_t sp1_crc504_bootstrap(uint32_t,const void*,uint32_t);
uint32_t sp1_crc504(uint32_t,const void*,uint32_t);
uint32_t sp1_crc513(uint32_t,const void*,uint32_t);
#endif
