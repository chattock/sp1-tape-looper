/* Read-only flash-mapped FC4D. Duplicate each chunk before USB export.
 * No generic host address, writes, launch, mutable status or SPI commands. */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
enum { ER_WAIT, ER_SEND, ER_DONE, ER_FAIL, ER_EMIT };
struct export_read {uint8_t chunk[240],command[9];unsigned size,pass,flash_offset,flash_blocks;int status,error;int64_t deadline;};
static int er_fail(struct export_read *v,int e){v->status=-1;v->error=e;return ER_FAIL;}
static int er_send(struct export_read *v,int64_t now){
 if(v->flash_offset>=0x80000)return er_fail(v,1);
 v->size=0x80000-v->flash_offset;if(v->size>240)v->size=240;
 uint32_t a=0xff000000u+v->flash_offset;
 v->command[0]=1;v->command[1]=0x4d;v->command[2]=0xfc;v->command[3]=5;
 for(unsigned i=0;i<4;i++)v->command[4+i]=(uint8_t)(a>>(i*8));
 v->command[8]=(uint8_t)v->size;v->deadline=now+500;return ER_SEND;
}
static int er_begin(struct export_read *v,int64_t now,unsigned arm){
 memset(v,0,sizeof *v);if(arm)return er_fail(v,8);return er_send(v,now);
}
static int er_poll(struct export_read *v,int64_t now,const uint8_t *b,unsigned n){
 if(v->error)return ER_FAIL;
 if(v->status==1)return ER_DONE;
 if(now>=v->deadline)return er_fail(v,2);
 unsigned total=7+v->size;
 if(n>total||(n&&b[0]!=4)||(n>1&&b[1]!=14)||(n>2&&b[2]!=total-3))return er_fail(v,3);
 if(n<total)return ER_WAIT;
 if(!b[3]||b[4]!=0x4d||b[5]!=0xfc||b[6])return er_fail(v,4);
 if(!v->pass){memcpy(v->chunk,b+7,v->size);v->pass=1;return er_send(v,now);}
 if(memcmp(v->chunk,b+7,v->size))return er_fail(v,5);
 v->flash_blocks++;return ER_EMIT;
}
static int er_emitted(struct export_read *v,int64_t now){
 if(v->error||v->status||v->pass!=1||!v->size||v->size>0x80000-v->flash_offset)return er_fail(v,10);
 v->flash_offset+=v->size;v->pass=0;
 if(v->flash_offset==0x80000){if(v->flash_blocks!=2185)return er_fail(v,6);v->status=1;return ER_DONE;}
 return er_send(v,now);
}
