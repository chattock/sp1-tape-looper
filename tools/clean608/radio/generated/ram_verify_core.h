/* Fixed-image RAM write/readback core. No radio launch, flash or generic address API.
 * H4 framing: Infineon 002-16618 Rev J sections 3.4.1 and 3.5.1.
 * Runs only after hardware recovery and successful HCI Reset handshake.
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "ram_image.h"
enum { RV_IDLE, RV_ENTRY, RV_SETTLE, RV_WRITE, RV_READ, RV_DONE, RV_ERROR };
enum { RV_WAIT=0, RV_SEND=1, RV_FINISH=2, RV_FAIL=-1 };
struct ram_verify {
 unsigned phase,offset,size,written,verified,bytes;
 uint8_t frame[258]; unsigned frame_len;
 int64_t deadline,overall,last;
 bool entry_silent;
 int error;
};
static int rv_fail(struct ram_verify *v,int why) { v->phase=RV_ERROR;v->error=why;return RV_FAIL; }
static bool rv_range(uint32_t a,unsigned n) {
 static const uint32_t ranges[][2]={{0xd0200,0xd24f8},{0x217a48,0x217a56}};
 for(unsigned i=0;i<sizeof ranges/sizeof ranges[0];++i)
  if(a>=ranges[i][0] && a<ranges[i][1] && n<=ranges[i][1]-a) return true;
 return false;
}
static int rv_write(struct ram_verify *v,int64_t now) {
 unsigned i=v->offset;
 if(i==sizeof ram_image) {
  if(v->verified!=39 || v->written!=39) return rv_fail(v,1);
  v->phase=RV_DONE;return RV_FINISH;
 }
 if(i>sizeof ram_image || sizeof ram_image-i<7) return rv_fail(v,2);
 unsigned n=ram_image[i+2];
 if(ram_image[i]!=0x4c || ram_image[i+1]!=0xfc || n<5 || n>253 || n+3>sizeof ram_image-i) return rv_fail(v,3);
 uint32_t a=(uint32_t)ram_image[i+3]|((uint32_t)ram_image[i+4]<<8)|((uint32_t)ram_image[i+5]<<16)|((uint32_t)ram_image[i+6]<<24);
 if(!rv_range(a,n-4)) return rv_fail(v,4);
 v->size=n-4;v->frame[0]=1;memcpy(v->frame+1,ram_image+i,n+3);
 v->frame_len=n+4;v->phase=RV_WRITE;v->deadline=now+500;return RV_SEND;
}
static int rv_begin(struct ram_verify *v,int64_t now) {
 memset(v,0,sizeof *v);v->phase=RV_ENTRY;v->last=now;v->overall=now+60000;v->deadline=now+500;
 const uint8_t cmd[]={1,0x2e,0xfc,0};memcpy(v->frame,cmd,sizeof cmd);v->frame_len=sizeof cmd;return RV_SEND;
}
static int rv_poll(struct ram_verify *v,int64_t now,const uint8_t *rx,unsigned n) {
 if(v->phase==RV_ERROR) return RV_FAIL;
 if(v->phase==RV_DONE) return RV_FINISH;
 if(v->phase==RV_IDLE) return RV_WAIT;
 if(now<v->last || now>=v->overall) return rv_fail(v,5);
 v->last=now;
 if(v->phase==RV_SETTLE) {
  if(n) return rv_fail(v,6);
  return now>=v->deadline?rv_write(v,now):RV_WAIT;
 }
 /* Only ENTRY silence is allowed: Rev J 3.2/3.4.1 documents autobaud mode.
  * Partial, malformed or rejected replies are never treated as silence. */
 if(now>=v->deadline) {
  if(v->phase==RV_ENTRY && n==0) {v->entry_silent=true;return rv_write(v,now);}
  return rv_fail(v,7);
 }
 unsigned expected=v->phase==RV_READ?7+v->size:7;
 if(n>expected || (n>0 && rx[0]!=4) || (n>1 && rx[1]!=14) || (n>2 && rx[2]!=expected-3)) return rv_fail(v,8);
 if(n<expected) return RV_WAIT;
 unsigned op=v->phase==RV_ENTRY?0x2e:(v->phase==RV_WRITE?0x4c:0x4d);
 if(rx[3]==0 || rx[4]!=op || rx[5]!=0xfc || rx[6]!=0) return rv_fail(v,9);
 if(v->phase==RV_ENTRY) {v->phase=RV_SETTLE;v->deadline=now+50;return RV_SEND; /* adapter clears consumed RX; no TX in SETTLE */}
 if(v->phase==RV_WRITE) {
  ++v->written;v->frame[0]=1;v->frame[1]=0x4d;v->frame[2]=0xfc;v->frame[3]=5;
  memcpy(v->frame+4,ram_image+v->offset+3,4);v->frame[8]=(uint8_t)v->size;
  v->frame_len=9;v->phase=RV_READ;v->deadline=now+500;return RV_SEND;
 }
 if(memcmp(rx+7,ram_image+v->offset+7,v->size)) return rv_fail(v,10);
 ++v->verified;v->bytes+=v->size;v->offset+=v->size+7;
 return rv_write(v,now);
}
