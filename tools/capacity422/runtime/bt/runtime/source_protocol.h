#ifndef SP1_VIDEO_SOURCE_PROTOCOL_H
#define SP1_VIDEO_SOURCE_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
static int video_hex(uint8_t c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;}
static int video_address_valid(const uint8_t*p){
 unsigned any=0,all=255;for(unsigned i=0;i<6;i++){any|=p[i];all&=p[i];}return any&&all!=255;
}
/* Exact21-byte command; lowercase hex avoids the console's A-prefix reset. */
static int video_parse_command(const char*s,size_t n,uint8_t out[7]){
 if(n!=21||memcmp(s,"A269SRC",7)||(s[7]!='0'&&s[7]!='2')||s[20]!='?')return 0;
 uint8_t tmp[7]={(uint8_t)(s[7]-'0')};
 for(unsigned i=0;i<6;i++){int a=video_hex(s[8+2*i]),b=video_hex(s[9+2*i]);if(a<0||b<0)return 0;tmp[i+1]=(uint8_t)((a<<4)|b);}
 if(!video_address_valid(tmp+1))return 0;
 memcpy(out,tmp,7);return 1;
}
static int video_packet_valid(const uint8_t*q,size_t n,uint32_t nonce,const uint8_t own[6],int already_configured){
 if(n!=16||already_configured||!nonce||memcmp(q,"SRC1",4)||(q[8]!=0&&q[8]!=2)||q[9])return 0;
 uint32_t got=q[4]|((uint32_t)q[5]<<8)|((uint32_t)q[6]<<16)|((uint32_t)q[7]<<24);
 return got==nonce&&video_address_valid(q+10)&&memcmp(q+10,own,6)!=0;
}
#endif
