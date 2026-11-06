#ifndef SP1_PERSISTENT_BOOT_GATE_H
#define SP1_PERSISTENT_BOOT_GATE_H
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
/* Dedicated controller-flash page, outside runtime, selector and maintenance.
 * Any non-erased word blocks normal boot, including partial/corrupt records.
 * Never erase this page until the entire restore target has been verified.
 * Reflashing another BIN is not a recovery operation and may erase this gate. */
#define SP1_GATE_ADDRESS 0x0007e000u
#define SP1_GATE_BYTES 4096u
#define SP1_GATE_MAGIC 0x47543153u
typedef struct {uint32_t magic;uint8_t hash[32];uint32_t crc;} sp1_gate_record;
static inline bool sp1_gate_blocked(const uint32_t *page){
 for(unsigned i=0;i<SP1_GATE_BYTES/4;i++)if(page[i]!=UINT32_MAX)return true;
 return false;
}
static inline uint32_t sp1_gate_crc(const uint8_t *p,unsigned n){
 uint32_t c=UINT32_MAX;while(n--){c^=*p++;for(unsigned i=0;i<8;i++)c=(c>>1)^(0xedb88320u&-(c&1u));}return ~c;
}
static inline bool sp1_gate_valid(const sp1_gate_record *r){
 return r->magic==SP1_GATE_MAGIC&&r->crc==sp1_gate_crc((const uint8_t*)r,36);
}
static inline void sp1_gate_make(sp1_gate_record *r,const uint8_t hash[32]){
 r->magic=SP1_GATE_MAGIC;memcpy(r->hash,hash,32);r->crc=sp1_gate_crc((const uint8_t*)r,36);
}
/* A torn initial lock may only be completed by programming the SAME bitwise
 * compatible record. No erase fallback; a conflicting target is refused. */
static inline bool sp1_gate_can_lock(const uint32_t *page,const sp1_gate_record *r){
 const uint32_t *words=(const uint32_t*)r;
 for(unsigned i=0;i<sizeof(*r)/4;i++)if((page[i]&words[i])!=words[i])return false;
 for(unsigned i=sizeof(*r)/4;i<SP1_GATE_BYTES/4;i++)if(page[i]!=UINT32_MAX)return false;
 return true;
}
#endif
