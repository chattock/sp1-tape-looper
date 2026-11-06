/* Project-owned, fixed wire envelope. Payload ABI is pinned by the generator.
 * CRC is corruption detection, not authentication or ownership proof.
 * A migration profile must independently audit VS layout and slot ownership.
 */
#ifndef SP1_RELEASE_BOND_POLICY_H
#define SP1_RELEASE_BOND_POLICY_H
#include <stdint.h>
#include <string.h>
enum { RB_BYTES=180, RB_PAYLOAD=32, RB_MAX=144 };
static uint32_t rb_crc(const uint8_t *p,unsigned n){
 uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;i++)c=(c>>1)^((0u-(c&1))&0xedb88320u);}return ~c;
}
static int rb_address(const uint8_t *a){
 unsigned zero=0,ff=0;for(unsigned i=0;i<6;i++){zero+=a[i]==0;ff+=a[i]==255;}return zero!=6&&ff!=6;
}
static void rb_make(uint8_t *r,unsigned kind,const uint8_t *a,const uint8_t *abi,const void *data,unsigned n,int valid){
 memset(r,0,RB_BYTES);memcpy(r,"TLB2",4);r[4]=2;r[5]=(uint8_t)kind;r[6]=(uint8_t)n;r[7]=(uint8_t)valid;
 memcpy(r+8,a,6);memcpy(r+14,abi,16);memcpy(r+RB_PAYLOAD,data,n);
 uint32_t c=rb_crc(r,RB_BYTES-4);for(unsigned i=0;i<4;i++)r[RB_BYTES-4+i]=(uint8_t)(c>>(8*i));
}
static int rb_owned(const uint8_t *r,unsigned len,unsigned kind,unsigned n,const uint8_t *a,const uint8_t *abi){
 if(len!=RB_BYTES||kind>1||n>RB_MAX||!rb_address(a)||memcmp(r,"TLB2",4)||r[4]!=2||r[5]!=kind||r[6]!=n||r[7]>1||memcmp(r+8,a,6)||memcmp(r+14,abi,16)||r[30]||r[31])return 0;
 for(unsigned i=RB_PAYLOAD+n;i<RB_BYTES-4;i++)if(r[i])return 0;
 uint32_t c=rb_crc(r,RB_BYTES-4);for(unsigned i=0;i<4;i++)if(r[RB_BYTES-4+i]!=(uint8_t)(c>>(8*i)))return 0;
 return 1;
}
#endif
