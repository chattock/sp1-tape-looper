#ifndef SP1_MODE_POLICY524_H
#define SP1_MODE_POLICY524_H
#include <stdint.h>
#include <stdbool.h>
/* Temporary entry requests are not recovery authority. Preserve the distinct
 * radio-write marker even when a separate media gate routes into storage. */
static inline uint8_t setup524_radio_entry(uint8_t marker){return marker==0xa7u?0:marker;}
static inline uint8_t setup524_storage_entry(uint8_t marker){return marker==0xa5u?marker:0;}
static inline uint8_t setup524_return(uint8_t marker,bool radio,bool boot){return marker==0xa5u?marker:boot?0:radio?0xa7u:0xa9u;}
#endif
