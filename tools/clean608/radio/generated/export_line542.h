/* Stack-only serialization. Exactly the existing ASCII wire format. */
#include <stddef.h>
#include <stdint.h>
static size_t export_line542(char out[512],unsigned post,unsigned offset,const uint8_t *bytes,unsigned size){
 static const char hex[]="0123456789abcdef";
 if(post>1||offset>=0x80000||!size||size>240||size>0x80000-offset)return 0;
 const char prefix[]="BTID335 FLASH ";size_t n=0;
 for(unsigned i=0;i<sizeof(prefix)-1;i++)out[n++]=prefix[i];
 out[n++]=(char)('0'+post);out[n++]=' ';
 for(int shift=16;shift>=0;shift-=4)out[n++]=hex[(offset>>shift)&15];
 out[n++]=' ';
 for(unsigned i=0;i<size;i++){out[n++]=hex[bytes[i]>>4];out[n++]=hex[bytes[i]&15];}
 out[n++]='\n';out[n]=0;return n;
}
