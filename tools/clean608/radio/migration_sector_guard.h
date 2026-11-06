#include "plan_limits.h"
/* Reusable bounded-plan qualification; NEVER include in runtime. */
/* Portable setup-side vendor sector engine. No hardware or USB drivers.
 * Caller arms ONLY after trusted profile, identity, stable backup and image-hash
 * checks. This engine independently bounds all flash-mapped vendor commands.
 * Session/sequence are USB envelope fields, not vendor HCI fields. CRC detects
 * corruption; it does not authenticate a release. Do not expose arm as raw USB.
 */
#ifndef SP1_MIGRATION_SECTOR_GUARD_H
#define SP1_MIGRATION_SECTOR_GUARD_H
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
enum { SG_BEGIN=0x1000, SG_LIMIT=0x1e000, SG_BYTES=4096, SG_CHUNK=240 };
enum sg_state { SG_IDLE, SG_UPLOAD, SG_READY, SG_WRITE, SG_READ, SG_DONE, SG_FAILED };
struct sector_guard {
 uint8_t data[SG_BYTES], reply[SG_CHUNK+7];
 uint64_t session, last_ms, deadline;
 uint32_t app_end, sequence, address, previous_end, upload, offset, received;
 unsigned count;
 enum sg_state state;
 bool inflight, clock_seen, armed;
};
static bool sg_fail(struct sector_guard *g){g->state=SG_FAILED;g->inflight=false;g->armed=false;return false;}
static bool sg_clock(struct sector_guard *g,uint64_t now){
 if(g->state==SG_FAILED||!g->armed)return false;
 if((g->clock_seen&&now<g->last_ms)||(g->inflight&&now>=g->deadline))return sg_fail(g);
 g->clock_seen=true;g->last_ms=now;return true;
}
static bool sg_arm(struct sector_guard *g,uint64_t session,uint32_t app_end){
 memset(g,0,sizeof *g);
 if(!session||app_end<=SG_BEGIN||app_end>SG_LIMIT||app_end%SG_BYTES)return sg_fail(g);
 g->session=session;g->app_end=app_end;g->previous_end=SG_BEGIN;g->state=SG_IDLE;g->armed=true;return true;
}
static bool sg_envelope(struct sector_guard *g,uint64_t session,uint32_t sequence){
 if(g->state==SG_FAILED||!g->armed)return false;
 if(session!=g->session||sequence!=g->sequence)return sg_fail(g);
 return true;
}
static bool sg_begin(struct sector_guard *g,uint64_t session,uint32_t sequence,uint32_t address){
 if(g->state==SG_FAILED||!g->armed)return false;
 if((g->state!=SG_IDLE&&g->state!=SG_DONE)||session!=g->session||
    g->sequence==UINT32_MAX||sequence!=g->sequence+1||
    !migration_allowed(address)||address<SG_BEGIN||address<g->previous_end||address%SG_BYTES||address>g->app_end-SG_BYTES)return sg_fail(g);
 g->sequence=sequence;g->address=address;g->upload=0;g->offset=0;g->state=SG_UPLOAD;return true;
}
static bool sg_upload(struct sector_guard *g,uint64_t session,uint32_t sequence,uint32_t offset,const uint8_t *data,unsigned n){
 if(!sg_envelope(g,session,sequence))return false;
 if(g->state!=SG_UPLOAD||offset!=g->upload||!data||!n||n>SG_CHUNK||n>SG_BYTES-g->upload)return sg_fail(g);
 memcpy(g->data+g->upload,data,n);g->upload+=n;return true;
}
static uint32_t sg_crc(const uint8_t *p,unsigned n){
 uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;i++)c=(c>>1)^(0xedb88320u& (0u-(c&1)));}return ~c;
}
static bool sg_seal(struct sector_guard *g,uint64_t session,uint32_t sequence,uint32_t crc){
 if(!sg_envelope(g,session,sequence))return false;
 if(g->state!=SG_UPLOAD||g->upload!=SG_BYTES||sg_crc(g->data,SG_BYTES)!=crc)return sg_fail(g);
 g->state=SG_READY;return true;
}
/* Emits only WRITE_RAM/READ_RAM at an independently guarded app address.
 * Caller needs a 248-byte command buffer. A watchdog-serviced transport must
 * poll sg_clock even without incoming bytes. No automatic resend after failure.
 */
static unsigned sg_command(struct sector_guard *g,uint64_t now,uint8_t out[248]){
 if(!sg_clock(g,now))return 0;
 if(g->inflight||(g->state!=SG_READY&&g->state!=SG_WRITE&&g->state!=SG_READ)){sg_fail(g);return 0;}
 if(now>UINT64_MAX-5000){sg_fail(g);return 0;}
 if(g->state==SG_READY)g->state=SG_WRITE;
 g->count=SG_BYTES-g->offset;if(g->count>SG_CHUNK)g->count=SG_CHUNK;
 uint32_t a=0xff000000u+g->address+g->offset;
 out[0]=1;out[1]=g->state==SG_WRITE?0x4c:0x4d;out[2]=0xfc;
 out[3]=g->state==SG_WRITE?4+g->count:5;
 for(unsigned i=0;i<4;i++)out[4+i]=(uint8_t)(a>>(i*8));
 if(g->state==SG_WRITE)memcpy(out+8,g->data+g->offset,g->count);else out[8]=g->count;
 g->inflight=true;g->received=0;g->deadline=now+5000;
 return g->state==SG_WRITE?8+g->count:9;
}
static bool sg_reply(struct sector_guard *g,uint64_t now,const uint8_t *data,unsigned n){
 if(!sg_clock(g,now))return false;
 unsigned total=7+(g->state==SG_READ?g->count:0);
 if(!g->inflight||!data||!n||n>total-g->received)return sg_fail(g);
 memcpy(g->reply+g->received,data,n);g->received+=n;
 uint8_t *b=g->reply;unsigned have=g->received;
 if(b[0]!=4||(have>1&&b[1]!=14)||(have>2&&b[2]!=total-3)||
    (have>3&&!b[3])||(have>4&&b[4]!=(g->state==SG_READ?0x4d:0x4c))||
    (have>5&&b[5]!=0xfc)||(have>6&&b[6]))return sg_fail(g);
 if(have<total)return true;
 if(g->state==SG_READ&&memcmp(b+7,g->data+g->offset,g->count))return sg_fail(g);
 g->inflight=false;g->offset+=g->count;
 if(g->offset==SG_BYTES){
  if(g->state==SG_WRITE){g->state=SG_READ;g->offset=0;}
  else{g->state=SG_DONE;g->previous_end=g->address+SG_BYTES;}
 }
 return true;
}
#endif
