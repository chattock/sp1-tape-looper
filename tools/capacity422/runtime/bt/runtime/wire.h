#ifndef MIDI_DUAL_WIRE_H
#define MIDI_DUAL_WIRE_H
#include <stdint.h>
#include <string.h>
#define MD_DATA 1
#define MD_CONTROL 2
#define MD_STATUS 3
#define MD_CLOCK 4
#define MD_ACK 5
static uint32_t md_get(const uint8_t*p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void md_put(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=v>>(8*i);}
static unsigned md_crc(const uint8_t*p,unsigned n){unsigned c=65535;while(n--){c^=(unsigned)*p++<<8;for(unsigned i=0;i<8;i++)c=(c<<1)^((c&32768)?0x1021:0);}return c&65535;}
static void md_finish(uint8_t*p){unsigned c=md_crc(p,30);p[30]=c;p[31]=c>>8;}
static int md_valid(const uint8_t*p,unsigned n,uint32_t seed){return n==32&&p[0]=='U'&&p[1]>=1&&p[1]<=5&&p[2]<=16&&md_get(p+4)==seed&&md_crc(p,30)==((unsigned)p[30]|(unsigned)p[31]<<8);}
static void md_init(uint8_t*p,unsigned kind,uint32_t seed,uint32_t now){memset(p,0,32);p[0]='U';p[1]=kind;md_put(p+4,seed);md_put(p+8,now);}
/* MIDI1 channel voice and single-byte real time, no silent SysEx truncation. */
static unsigned md_midi_len(const uint8_t*p){unsigned cin=p[0]&15,s=p[1],n=cin==12||cin==13?2:cin>=8&&cin<=14?3:cin==15?1:0;if(!n||p[0]>>4)return 0;if(n==1)return s>=0xf8?1:0;if((s>>4)!=cin||p[2]>127||(n==3&&p[3]>127))return 0;return n;}
#endif
