/* Project-authored compatibility functions for six-byte Bluetooth addresses. */
#include <stdint.h>
#include <string.h>
void utl_bdcpy(uint8_t *d,const uint8_t *s){memcpy(d,s,6);}
int utl_bdcmp(const uint8_t *a,const uint8_t *b){return memcmp(a,b,6);}
