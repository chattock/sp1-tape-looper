#ifndef VIDEO_STANDALONE_H
#define VIDEO_STANDALONE_H
#include <stdint.h>
static const uint8_t video_receiver_id[8]={0xa0,0x71,0xd2,0xa3,0x1d,0x58,0xef,0x50};
/* Dedicated B -> A kit, matching the already-proved host address derivation.
 * Not generic discovery, authentication, or a persistent bond operation. */
static const uint8_t video_sender_id[8]={0xb8,0x69,0x53,0x70,0x29,0xc0,0x14,0x2e};
static const uint8_t video_receiver_address[6]={0xc2,0x03,0x6c,0x8a,0x4c,0x4d};
static inline int video_standalone_decide(int safe,int correct_sender,unsigned phase,
 unsigned mode,int configured,unsigned reports,uint32_t magic,unsigned version,unsigned source_phase){
 if(!safe||!correct_sender)return -1;
 if(configured||phase!=4||mode!=1||!reports||magic!=0x31525356||version!=1||source_phase!=2)return 0;
 return 1;
}
/* 1 receiver, 2 sender; unknown devices must not start this fixed-pair build. */
static inline unsigned video_kit_role(const uint8_t id[8]) {
 unsigned a=0,b=0;
 for(unsigned i=0;i<8;i++){a|=id[i]^video_receiver_id[i];b|=id[i]^video_sender_id[i];}
 return !a?1u:(!b?2u:0u);
}
#endif
