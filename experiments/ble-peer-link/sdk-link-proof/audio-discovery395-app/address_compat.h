/* Address utilities used by the vendor A2DP profile but absent in the pinned
 * 20706 exported interface. Bytewise operations only, no radio behavior. */
#include <stdint.h>
#include <string.h>
static inline void utl_bdcpy(uint8_t *d,const uint8_t *s){memcpy(d,s,6);}
static inline int utl_bdcmp(const uint8_t *a,const uint8_t *b){return memcmp(a,b,6);}
