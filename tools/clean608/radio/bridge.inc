/* Bounded host-streamed bench adapter. No vendor opcode passthrough.
 * Recovery trusts the original local host journal, not an unsigned host claim
 * of release ownership. Static/reserved sectors are inaccessible in all modes. */
#ifndef SP1_BENCH_NATIVE_TEST
#include <zephyr/drivers/entropy.h>
#endif
#include <stdlib.h>
#include "migration_sector_guard.h"
#include "admission.h"
#include "vs_policy.h"
static struct admission admission;
static struct sector_guard sector;
static uint8_t sector_command[248];
static uint64_t challenge;
static unsigned plan_index,host_seq,mode;
static bool plan_received_header,write_started,write_verified,post_read,arm_received,protocol_error;
static char host_line[600];static unsigned host_length;
static int64_t host_deadline;
static bool hexnum(const char*s,unsigned digits,uint64_t*out){
 if(strlen(s)!=digits||digits>16)return false;
 *out=0;
 while(*s){unsigned v=*s>='0'&&*s<='9'?*s-'0':*s>='a'&&*s<='f'?*s-'a'+10:99;if(v>15)return false;*out=(*out<<4)|v;s++;}return true;
}
static bool unhex(const char*s,uint8_t*p,unsigned n){
 if(strlen(s)!=n*2)return false;
 for(unsigned i=0;i<n;i++){char b[3]={s[2*i],s[2*i+1],0};uint64_t v;if(!hexnum(b,2,&v))return false;p[i]=v;}return true;
}
static bool bridge_challenge(void){
 const struct device *entropy=DEVICE_DT_GET(DT_CHOSEN(zephyr_entropy));
 if(!device_is_ready(entropy)||entropy_get_entropy(entropy,(void *)&challenge,sizeof challenge)||!challenge)return false;
 printk(DIAG_TAG " READY_PLAN %08x%08x\n",(uint32_t)(challenge>>32),(uint32_t)challenge);return true;
}
#include "clean607.h"
static bool CLEAN607_CODE bridge_admit(void){
 if(!admission_complete(&admission)||!plan_received_header||plan_received!=plan_count)return false;
 if(mode==2&&!plan_count)return true; /* Inspection may read unknown sources, never write. */
 if(mode==3)return clean607_admit();
 if(mode==1)return !clean607_pending(); /* Explicit recovery: host journal binds original bytes. */
 bool zero=true,ff=true;for(unsigned i=0;i<6;i++){zero&=local_address[i]==0;ff&=local_address[i]==255;}
 if(!admission.layout_ok||zero||ff||!(source_known(&admission)||source_c_candidate(&admission)||source_b_candidate(&admission)||source_feldd565(&admission))||memcmp(before_hashes[2],erased_hash,32))return false;
 if(mode==2){for(unsigned i=0;i<plan_count;i++)if(memcmp(before_hashes[plan_pages[i]],after_hashes[plan_pages[i]],32))return false;return true;}
 if(!vs_target())return false;
 uint8_t h[32];sector_hash(expected_vs,h);if(memcmp(h,after_hashes[1],32))return false;
 for(unsigned i=0;i<27;i++)if(memcmp(after_hashes[i+3],target_sectors_hash[i],32))return false;
 return true;
}
static void bridge_post(uint64_t now){
 write_verified=true;
 if(!plan_count){
  /* Initial duplicate-read already proved the entire unchanged expected image. */
  printk(DIAG_TAG " PLAN_VERIFIED sectors=0 no_write=1\n");result=1;finish();return;
 }
 post_read=true;memset(&admission,0,sizeof admission);
 printk(DIAG_TAG " PLAN_VERIFIED sectors=%u begin_full_readback=1\n",plan_count);
 unsigned k=irq_lock();er_begin(&er,now,0);rx_n=tx_n=0;tx_query=er.command;tx_len=9;
 phase=READ_EXPORT;uart_irq_tx_enable(radio);irq_unlock(k);
}
static bool bridge_job(uint64_t now){
 if(plan_index>=plan_count||!sg_begin(&sector,challenge,plan_index+1,plan_address(plan_index)))return false;
 phase=UPLOAD_SECTOR;deadline=now+90000;
 printk(DIAG_TAG " SECTOR_READY index=%u address=%05x\n",plan_index+1,plan_address(plan_index));return true;
}
static bool bridge_command(char*s){
 char*t[8];unsigned n=0;
 /* Exact single-space tokenization: no ignored suffix, whitespace or numbers. */
 if(!*s||*s==' ')return false;
 t[n++]=s;
 for(char*p=s;*p;p++)if(*p==' '){*p=0;if(!p[1]||p[1]==' '||n==8)return false;t[n++]=p+1;}
 uint64_t nonce,seq,v,w;
 if(n<3||!hexnum(t[1],16,&nonce)||nonce!=challenge||!challenge||!hexnum(t[2],8,&seq)||seq!=host_seq+1||host_seq==UINT32_MAX)return false;
 if(!strcmp(t[0],"PLAN335")){
  if(n!=6||phase!=WAIT_ARM||plan_received_header||!hexnum(t[3],16,&v)||v!=((uint64_t)NRF_FICR->DEVICEID[1]<<32|NRF_FICR->DEVICEID[0])||!hexnum(t[4],2,&v)||v>3||!hexnum(t[5],2,&w)||w>MAX_PLAN_SECTORS)return false;
  if(sp1_setup_recovery_required()&&v!=1&&!(v==3&&clean607_pending()))return false;
  mode=v;plan_count=w;plan_received_header=true;
 }else if(!strcmp(t[0],"HASH335")){
  uint8_t hash[32];
  if(n!=5||phase!=WAIT_ARM||!plan_received_header||plan_received>=plan_count||!hexnum(t[3],5,&v)||!(allowed_address(v)||(mode==3&&v==0x2000))||(plan_received&&v<=plan_address(plan_received-1))||!unhex(t[4],hash,32))return false;
  plan_pages[plan_received++]=v/4096;memcpy(after_hashes[v/4096],hash,32);
 }else if(!strcmp(t[0],"ARM335")){
  if(n!=3||phase!=WAIT_ARM||arm_received||!bridge_admit())return false;
  arm_received=true;
 }else if(!strcmp(t[0],"DATA335")){
  uint8_t data[240];unsigned len=n==6?strlen(t[5]):0;
  if(n!=6||phase!=UPLOAD_SECTOR||!hexnum(t[3],2,&v)||v!=plan_index+1||!hexnum(t[4],4,&w)||!len||len>480||len%2||!unhex(t[5],data,len/2)||!sg_upload(&sector,challenge,plan_index+1,w,data,len/2))return false;
 }else if(!strcmp(t[0],"SEAL335")){
  uint8_t hash[32];
  if(n!=4||phase!=UPLOAD_SECTOR||!hexnum(t[3],2,&v)||v!=plan_index+1||sector.upload!=4096)return false;
  sector_hash(sector.data,hash);if(memcmp(hash,after_hashes[sector.address/4096],32)||!sg_seal(&sector,challenge,plan_index+1,sg_crc(sector.data,4096)))return false;
  /* No flash command until the ENTIRE sector matches its admitted SHA256. */
  if(mode==3&&!clean607_begin())return false;
  sp1_setup_write_begin();write_started=true;phase=WRITE_SECTOR;
  unsigned k=irq_lock();tx_len=sg_command(&sector,k_uptime_get(),sector_command);tx_query=sector_command;tx_n=rx_n=0;if(tx_len)uart_irq_tx_enable(radio);irq_unlock(k);if(!tx_len)return false;
 }else return false;
 host_seq=seq;printk(DIAG_TAG " ACK %08x\n",host_seq);return true;
}
static void bridge_byte(uint8_t b){
 if(protocol_error)return;
 if(!host_length)host_deadline=k_uptime_get()+5000;
 if(b!='\n'){
  if(b<32||b>126||host_length>=sizeof host_line-1){protocol_error=true;return;}host_line[host_length++]=b;return;
 }
 host_line[host_length]=0;host_length=0;
 if(!used&&!active){if(atomic_get(&requested)||strcmp(host_line,"SP1PLAN335")){protocol_error=true;return;}atomic_set(&requested,1);}
 else if(!active||!bridge_command(host_line))protocol_error=true;
}
static void bridge_service(int64_t now){
 if(phase==WAIT_ARM||phase==UPLOAD_SECTOR){
  if(now>=deadline){result=-32;finish();return;}
  if(phase==WAIT_ARM&&arm_received){
   if(!sg_arm(&sector,challenge,0x1e000)){result=-33;finish();return;}
   if(!plan_count){bridge_post(now);return;}
   plan_index=0;if(!bridge_job(now)){result=-33;finish();}
  }return;
 }
 if(phase!=WRITE_SECTOR)return;
 unsigned key=irq_lock();bool okay=sg_clock(&sector,now);
 if(okay&&rx_n){okay=tx_n==tx_len&&sg_reply(&sector,now,rx,rx_n);rx_n=0;}
 if(okay&&!sector.inflight&&sector.state!=SG_DONE){uart_irq_tx_disable(radio);tx_len=sg_command(&sector,now,sector_command);tx_n=0;okay=tx_len!=0;if(okay)uart_irq_tx_enable(radio);}
 irq_unlock(key);
 if(!okay){result=-34;finish();return;}
 if(sector.state==SG_DONE){
  printk(DIAG_TAG " SECTOR_VERIFIED index=%u address=%05x\n",plan_index+1,plan_address(plan_index));
  if(++plan_index<plan_count){if(!bridge_job(now)){result=-34;finish();}return;}bridge_post(now);
 }
}
