#ifndef SP1_CODEC_POLICY_H
#define SP1_CODEC_POLICY_H
#include <stdint.h>
/* SBC codec IE starts at LOSC. Keep I2S at48k, stereo,16 blocks/8 bands.
 * Only negotiate format parameters supported by the existing encoder path. */
static unsigned sp1_sbc_select(const uint8_t *p,unsigned size,uint8_t out[7]){
 if(!p||size<7||p[0]!=6||p[1]!=0||p[2]!=0)return 1;
 if(!(p[3]&0x10)||!(p[3]&3)||(p[4]&0x14)!=0x14||!(p[4]&3))return 2;
 if(p[5]<2||p[6]>250||p[5]>p[6]||p[5]>53)return 3;
 out[0]=6;out[1]=0;out[2]=0;out[3]=0x10|((p[3]&1)?1:2);
 out[4]=0x14|((p[4]&1)?1:2);out[5]=p[5];out[6]=p[6]<53?p[6]:53;
 return 0;
}
#endif
