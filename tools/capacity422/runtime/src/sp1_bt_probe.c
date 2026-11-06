#include "storage373.h"
#ifndef SP1_CAPACITY_BUILD
#define SP1_CAPACITY_BUILD 422
#endif
#define CAPACITY_STRING1(x) #x
#define CAPACITY_STRING(x) CAPACITY_STRING1(x)
#include <zephyr/sys/reboot.h>
/* Unified fixed-pair controller259; physical discovery selects this kit's exact A receiver.
 * Serial tests may still configure an explicit target and route.
 * Resource reports are replaced by source diagnostics to avoid extra RAM.
 * Existing receiver audio engine remains unchanged. Runtime commands only;
 * this controller contains no radio flash programmer. */
#include "sp1_bt_probe.h"
#include "ownership.h"
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <soc.h>
#include <errno.h>
#include "audio_decode.h"
#include "workspace.h"
#include "lifecycle.h"
#include "timeline.h"
#include "source_protocol.h"
#include "standalone.h"
#include "return_policy.h"
#include "companion_storage_policy.h"
extern bool bt308_companion_storage(void);
#include "wire.h"
extern void sp1_usb_midi_poll(void);
extern int sp1_usb_midi_ready(void),sp1_usb_midi_send(const uint8_t*,unsigned);
extern bool bt308_return_safe(void);
extern bool storage402_faulted(void);
extern void bt308_return_report(void);
extern bool bt308_stopped(void),bt308_safe(void),bt308_setup_safe(void);
extern bool bt339_connect_safe(void),bt339_input_switch_safe(void),bt340_usb_input(void);
static bool input_selected;
static int16_t input_last_l,input_last_r;
extern unsigned bt308_context(void),bt308_output_errors(void),bt308_output_max_us(void),bt308_record_health(unsigned);
extern void bt308_profile_arm(void),bt308_detail_report(void),bt308_snapshot(uint8_t *),bt308_snapshot_base(uint8_t *);
static const struct device *const radio=DEVICE_DT_GET(DT_NODELABEL(uart0));
static const struct device *const console=DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
/* owner:0 closed;1 audio enabled;2 audio render in flight. Close with CAS,
 * never memset the overlay while a preempted renderer still owns it. */
static atomic_t owner,request,info_request;
bool sp1_drive409_request(void){return atomic_cas(&request,0,98);}
static bool initialized,active,announced,done,armed_print,accept_audio,wedged;
static volatile bool rx_enabled;
static uint8_t mode,pending,phase,ui,handoff;static bool view_open,cue_pending;static int result;
static uint32_t begin_at,finish_at,select_at,seed,ui_at,switches,view_at;
static struct bt_trace trace;
/* kind 1 gap opens; 2 gap recovers (48 kHz samples); 3 received radio flags.
 * Timestamp is controller observation, not phone/A2DP or end-to-end latency. */
static void trace_event(unsigned kind,unsigned value){
 unsigned key=irq_lock();bt_trace_add(&trace,k_uptime_get_32(),kind,value);irq_unlock(key);
}
/* Explicitly armed diagnostic only. Reuse the existing 24-event trace.
 * USB enqueue→USB ingress, common millisecond clock; excludes playback ring
 * latency and instrument attack cannot be separated from host delay. */
static volatile bool return_armed;
static unsigned return_quiet;
void bt308_return_input(const int16_t *pcm,unsigned samples){
 if(!return_armed||mode!=5||phase!=4||!samples)return;
 unsigned peak=0;for(unsigned i=0;i<samples;i++){int v=pcm[i];unsigned a=v<0?-v:v;if(a>peak)peak=a;}
 unsigned key=irq_lock();
 if(return_armed){
  if(peak<128){if(return_quiet<4800)return_quiet+=samples;}
  else {if(peak>=1024 && return_quiet>=4800)bt_trace_add(&trace,k_uptime_get_32(),5,peak);return_quiet=0;}
  if(trace.count>=BT_TRACE_CAPACITY)return_armed=false;
 }
 irq_unlock(key);
}
void audio_decode_gap_event(int open,unsigned samples){trace_event(open?1:2,samples);}
static uint32_t source_report[10],source_reports;
/* Discovery and companion snapshots are mutually exclusive radio roles. */
#define audio_search ((uint32_t*)discovery_report)
#define audio_candidates ((uint8_t (*)[7])peer.snapshot_scratch)
static uint8_t search_pending; /* 1 awaiting boot; 2 discovery owns scratch. */
static uint8_t discovery_report[40] __attribute__((aligned(4)));static unsigned discovery_reports,discovery_bad;
static uint32_t unified[10],status_staging[10];static unsigned status_n,status_reports;
static uint8_t dma_buf[2][256] __aligned(4);static bool dma_busy[2];
static uint8_t header[5];static unsigned hn,left,payload_n;static uint16_t opcode;
/* Retain first fault. One bounded outgoing-start RX rearm; never replay commands. */
static struct { uint32_t count,ms,reason,position,radio_tick,rx_len,state,hfclk,pins,rx_amount,tx_amount; } uart_fault;
static uint32_t rx_rearm_at,rx_rearm_reports;
static uint8_t rx_rearms,rx_rearm_wait;
static volatile unsigned bytes,packets,audio_packets,audio_bytes,errors,discard,dma_bad;
static unsigned monitor_max_us,input_blocks,playing_blocks,recording_blocks,record_audio_frames;
static struct {unsigned blocks,max_us,over;} budgets[5];static unsigned block_stage;
static char command[25];static unsigned cn;
static uint8_t source_pending[7],source_target[6],source_route;
static bool source_configured,standalone_pending;
static uint32_t relay_report[10],relay_reports;
static uint32_t source_rejected;

#define peer (bt_workspace.peer)
#define nonce (bt_workspace.rng)
#if defined(CONFIG_ENTROPY_GENERATOR) || defined(CONFIG_NRFX_RNG)
#error "Unified prototype requires exclusive RNG ownership"
#endif
static void send_frame(unsigned,const uint8_t*,unsigned);
struct midi_usb_q {uint32_t w,r,clock_at;uint8_t packets[32][4],control[8],pending,overflow;};
#define mq (*(struct midi_usb_q*)peer.snapshot_scratch)
_Static_assert(sizeof(struct midi_usb_q)<=sizeof peer.snapshot_scratch,"MIDI queue overlay size");
void bt308_usb_packet(const uint8_t p[4]){
 if(mode!=5||phase!=4||!md_midi_len(p))return;
 unsigned key=irq_lock();if(mode!=5||phase!=4){irq_unlock(key);return;}if(mq.w-mq.r<32){memcpy(mq.packets[mq.w%32],p,4);mq.w++;}else {++peer.invalid;mq.overflow=1;}irq_unlock(key);
}
void bt308_usb_detach(void){if(mode==5&&phase==4){unsigned k=irq_lock();if(mode==5&&phase==4)mq.r=mq.w;irq_unlock(k);}}
static void midi_usb_service(uint32_t now){
 uint8_t p[32];if(mq.pending){md_init(p,MD_CONTROL,seed,now);p[2]=8;unsigned k=irq_lock();memcpy(p+12,mq.control,8);mq.pending=0;irq_unlock(k);md_finish(p);send_frame(0x7e11,p,32);}if(now-mq.clock_at>=20){md_init(p,MD_CLOCK,seed,now);p[2]=2;p[12]=sp1_usb_midi_ready();p[13]=mq.overflow;mq.overflow=0;md_finish(p);send_frame(0x7e11,p,32);mq.clock_at=now;}
 unsigned key=irq_lock(),n=mq.w-mq.r;if(n>4)n=4;
 md_init(p,MD_DATA,seed,now);p[2]=n*4;for(unsigned i=0;i<n;i++)memcpy(p+12+4*i,mq.packets[(mq.r+i)%32],4);mq.r+=n;irq_unlock(key);
 if(n){md_finish(p);send_frame(0x7e11,p,32);}
}

static void rng_stop(void){if(nonce.status==1){NRF_RNG->TASKS_STOP=1;NRF_RNG->EVENTS_VALRDY=0;NRF_RNG->CONFIG=nonce.saved_config;nonce.status=0;}}
static int rng_begin(uint32_t now){
 memset(&bt_workspace,0,sizeof bt_workspace);
 if(NRF_RNG->INTENSET||NRF_RNG->SHORTS)return -1;
 nonce.saved_config=NRF_RNG->CONFIG;nonce.at=now;nonce.status=1;
 NRF_RNG->TASKS_STOP=1;NRF_RNG->CONFIG=1;NRF_RNG->EVENTS_VALRDY=0;NRF_RNG->TASKS_START=1;return 0;
}
static int rng_poll(uint32_t now){
 if(now-(uint32_t)nonce.at>=8000){rng_stop();return -1;}
 if(!NRF_RNG->EVENTS_VALRDY)return 0;
 nonce.word|=(NRF_RNG->VALUE&255u)<<(8*(nonce.bytes%4));NRF_RNG->EVENTS_VALRDY=0;
 if(++nonce.bytes%4)return 0;
 if(!nonce.word||nonce.word>0xffff0000u){rng_stop();return -1;}
 for(unsigned i=0;i<nonce.count;i++)if(nonce.words[i]==nonce.word){rng_stop();return -1;}
 nonce.words[nonce.count++]=nonce.word;nonce.word=0;
 if(nonce.count<8)return 0;seed=nonce.words[7];rng_stop();return 1;
}
static void ownership(unsigned stage){
 printk("BTID329 OWNERSHIP stage=%u uart_enable=%u cs_cnf=%08x cs_out=%u reset_cnf=%08x reset_out=%u rts_cnf=%08x rts_out=%u\n",stage,NRF_UARTE0->ENABLE,NRF_P1->PIN_CNF[5],!!(NRF_P1->OUT&32u),NRF_P0->PIN_CNF[10],!!(NRF_P0->OUT&1024u),NRF_P1->PIN_CNF[1],!!(NRF_P1->OUT&2u));
}
static void reset(bool high){if(high)NRF_P0->OUTSET=1u<<10;else NRF_P0->OUTCLR=1u<<10;NRF_P0->PIN_CNF[10]=1;}
static void feedback(unsigned value){if(ui!=value){ui=value;ui_at=k_uptime_get_32();cue_pending=value==3||value==4||value==5;if(value==3||value==4||value==5||value==0)view_open=false;}}
bool sp1_bt_probe_quiet(void){return true;}
/* API uses radio mode IDs; 0 off. Menu toggles the current selection off. */
static bool selected(void);
bool sp1_bt_output_live(void){
 return active&&mode==7&&phase==4&&selected()&&!errors&&!dma_bad&&rx_enabled&&
   k_uptime_get_32()-select_at<3000u&&(unified[9]&3u)==3u&&
   source_report[0]==0x31525356&&(source_report[1]==3||source_report[1]==4)&&source_report[2]==7&&!source_report[3];
}
bool sp1_bt_peer_view_open(void){return (view_open||cue_pending)&&bt339_connect_safe();}
void sp1_bt_peer_request(unsigned choice){if(choice==8||choice==9){atomic_set(&request,choice==8?42:43);return;}if(choice==7){atomic_set(&request,22);return;}if(choice<=6)atomic_set(&request,choice==6?20:10+bt_choice(mode,choice));}
unsigned sp1_bt_peer_lights(void){
 if(!bt339_connect_safe())return 0;
 uint32_t now=k_uptime_get_32();
 if(cue_pending){unsigned cue=bt_led_pattern(mode,ui,now-ui_at,now);if(cue)return cue;}
 if(!view_open)return 0;
 if((relay_report[2]==1||relay_report[2]==2))return bt_led_pattern(1,1,now-view_at,now);
 if((mode==1&&(unified[9]&1))||sp1_bt_output_live())return 0x1f0;
 if(ui==3)return 0x1f0;
 return bt_led_pattern(mode==7?1:mode,mode==3?2:1,now-view_at,now);
}
static void send_frame(unsigned op,const uint8_t*p,unsigned n){
 uint8_t h[5]={0x19,op,op>>8,n,n>>8};
 for(unsigned i=0;i<5;i++)uart_poll_out(radio,h[i]);
 for(unsigned i=0;i<n;i++)uart_poll_out(radio,p[i]);
}
static void enqueue(unsigned n){
 uint32_t w=__atomic_load_n(&peer.write,__ATOMIC_RELAXED),r=__atomic_load_n(&peer.read,__ATOMIC_ACQUIRE);
 if(n>40||w-r>=8){++dma_bad;return;}
 memcpy(peer.fifo[w%8],peer.incoming,n);peer.length[w%8]=n;
 __atomic_store_n(&peer.write,w+1,__ATOMIC_RELEASE);
}
extern void bt308_app_disconnect(void);
extern void bt308_app_bulk_byte(unsigned,uint8_t);
extern void bt308_app_byte(unsigned,uint8_t),bt308_app_ready(unsigned),bt308_app_sent(void);
extern unsigned bt308_app_reply(const uint8_t **);
static void byte_in(uint8_t b){
 ++bytes;
 if(left){
  if(opcode==0x7e30&&mode==4)bt308_app_byte(payload_n,b);
  if(opcode==0x7e31&&mode==4)bt308_app_bulk_byte(payload_n,b);
  if((opcode==0x7e07||opcode==0x7e08||opcode==0x7e06||opcode==0x7e00||opcode==0x7e04||opcode==0x7e05||(opcode==0x7e10&&mode==6))&&status_n<40)((uint8_t*)status_staging)[status_n++]=b;
  if(mode>=2&&mode<=5&&(opcode==0x7e10||opcode==0x7e11)&&payload_n<40)peer.incoming[payload_n]=b;
  if(mode==1&&opcode==0x140a&&accept_audio)audio_decode_byte(b);
  ++payload_n;
  if(!--left){
   if(opcode==0x7e30&&mode==4)bt308_app_ready(payload_n);
   if(opcode==0x7e00&&status_n==40&&payload_n==40){if(!status_reports||((unified[9]^status_staging[9])&3u))trace_event(3,status_staging[9]&3u);memcpy(unified,status_staging,40);++status_reports;if(mode==7&&phase==4)select_at=k_uptime_get_32();}
   if(opcode==0x7e06&&status_n==40&&payload_n==40&&status_staging[0]==0x31594c52&&status_staging[1]==1&&status_staging[4]==seed){
    memcpy(relay_report,status_staging,40);++relay_reports;
    if(relay_report[2]==3)feedback(3);else if(relay_report[2]==4)feedback(4);
   }
   if(opcode==0x7e05&&status_n==16&&payload_n==16&&(mode==6||mode==1)&&phase==4){
    uint8_t *p=(uint8_t*)status_staging;
    if(!memcmp(p,"ROL1",4)&&sp1_link_get(p+4)==seed&&p[8]==7&&!p[9]&&!atomic_get(&request)){
     unsigned any=0;for(unsigned i=10;i<16;i++)any|=p[i];
     if(any&&mode==1){memcpy(source_target,p+10,6);}
     else if(any){source_pending[0]=0;memcpy(source_pending+1,p+10,6);atomic_cas(&request,0,31);}
    }
   }
   if(opcode==0x7e10&&mode==6&&!search_pending&&status_n==40&&payload_n==40){
    uint8_t *p=(uint8_t*)status_staging;
    if(!memcmp(p,"SPR1",4)&&p[4]==1&&sp1_link_get(p+8)==seed&&sp1_link_crc(p,38)==((unsigned)p[38]|((unsigned)p[39]<<8))){memcpy(discovery_report,p,40);++discovery_reports;}else ++discovery_bad;
   }
   if(opcode==0x7e07&&status_n==40&&payload_n==40&&status_staging[0]==0x31444341)memcpy(audio_search,status_staging,40);
   if(opcode==0x7e08&&status_n==16&&payload_n==16){uint8_t *p=(uint8_t*)status_staging;if(!memcmp(p,"ADR1",4)&&p[4]<8)memcpy(audio_candidates[p[4]],p+5,7);}
   if(opcode==0x7e04&&status_n==40&&payload_n==40){memcpy(source_report,status_staging,40);++source_reports;}
   if(mode>=2&&mode<=5&&((opcode==0x7e10&&(payload_n==32||payload_n==40))||(opcode==0x7e11&&(payload_n==24||payload_n==32))))enqueue(payload_n);
   if(opcode==0x140a)++audio_packets;++packets;
  }return;
 }
 if(!hn&&b!=0x19){++discard;return;}
 header[hn++]=b;if(hn<5)return;hn=0;opcode=header[1]|((unsigned)header[2]<<8);left=header[3]|((unsigned)header[4]<<8);payload_n=0;
 if(left>4096){left=0;++discard;return;}
 if(opcode==0x7e07||opcode==0x7e08||opcode==0x7e06||opcode==0x7e00||opcode==0x7e04||opcode==0x7e05||opcode==0x7e10)status_n=0;
 if(opcode==0x140a){audio_bytes+=left;accept_audio=mode==1&&audio_decode_packet(left);}
 if(!left)++packets;
}
static void bytes_in(const uint8_t*p,unsigned n){
 while(n){if(mode==1&&left&&opcode==0x140a){unsigned take=n<left?n:left;if(accept_audio)audio_decode_bytes(p,take);bytes+=take;left-=take;payload_n+=take;p+=take;n-=take;if(!left){++packets;++audio_packets;}}else{byte_in(*p++);--n;}}
}
static void rx_event(const struct device*d,struct uart_event*e,void*u){
 (void)u;
 switch(e->type){
 case UART_RX_RDY:{uint8_t*p=e->data.rx.buf;unsigned off=e->data.rx.offset,n=e->data.rx.len;if((p!=dma_buf[0]&&p!=dma_buf[1])||off>256||n>256-off){++dma_bad;break;}bytes_in(p+off,n);break;}
 case UART_RX_BUF_REQUEST:
  for(unsigned i=0;i<2;i++)if(!dma_busy[i]){dma_busy[i]=true;if(uart_rx_buf_rsp(d,dma_buf[i],256)<0){dma_busy[i]=false;++dma_bad;}return;}++dma_bad;break;
 case UART_RX_BUF_RELEASED:
  if(e->data.rx_buf.buf==dma_buf[0])dma_busy[0]=false;else if(e->data.rx_buf.buf==dma_buf[1])dma_busy[1]=false;else ++dma_bad;break;
 case UART_RX_STOPPED:
  if(!uart_fault.count){
   uart_fault.hfclk=NRF_CLOCK->HFCLKSTAT;uart_fault.pins=NRF_P1->IN;
   uart_fault.rx_amount=NRF_UARTE0->RXD.AMOUNT;uart_fault.tx_amount=NRF_UARTE0->TXD.AMOUNT;
   uart_fault.ms=k_uptime_get_32();uart_fault.reason=(unsigned)e->data.rx_stop.reason;
   uart_fault.position=((uint32_t)opcode<<16)|(left&0xffffu);
   uart_fault.radio_tick=unified[7];uart_fault.rx_len=e->data.rx_stop.data.len;
   uart_fault.state=mode|((uint32_t)phase<<8)|((uint32_t)dma_busy[0]<<16)|((uint32_t)dma_busy[1]<<17);
  }
  ++uart_fault.count;errors|=(unsigned)e->data.rx_stop.reason;break;
 case UART_RX_DISABLED:rx_enabled=false;break;
 default:break;
 }
}
static void stop(int code){
 standalone_pending=false;
 if(phase==1)rng_stop();result=code;active=false;phase=5;finish_at=k_uptime_get_32();
 if(code<0)feedback(4);else feedback(0);
 /* Zephyr error_isr owns disable after RX_STOPPED; wait for RX_DISABLED. */
 if(rx_enabled&&!errors&&uart_rx_disable(radio)<0){wedged=true;++dma_bad;}
}
void sp1_bt_probe_stop(void){
 pending=0;view_open=false;stop(0);
 /* Power-off/DFU never resumes the main loop. Assert hardware reset NOW,
  * without clearing storage that an in-flight renderer may still own. */
 reset(false);NRF_P1->OUTCLR=2u;
}
bool bt308_storage_allowed(void){return !mode||(mode==4&&phase==4);}
static bool selected(void){return status_reports&&unified[0]==0x31555053&&unified[1]==2&&unified[2]==1&&unified[3]==mode&&unified[4]==seed&&!unified[5]&&!unified[6];}
static void select_mode(void){uint8_t p[20]={'S','P','U','2',2,mode,0,0},snap[56];sp1_link_put(p+8,seed);bt308_snapshot_base(snap);memcpy(p+12,snap+48,8);p[12]^=0x29;send_frame(0x7e00,p,20);}
static void peer_service(uint32_t now){
 uint32_t r=__atomic_load_n(&peer.read,__ATOMIC_RELAXED),w=__atomic_load_n(&peer.write,__ATOMIC_ACQUIRE);
 while(r!=w){uint8_t p[40];unsigned n=peer.length[r%8];memcpy(p,peer.fifo[r%8],n);
  if(mode==5&&n==32&&md_valid(p,n,seed)&&p[1]==MD_DATA&&p[2]&&!(p[2]%4)&&sp1_usb_midi_ready()&&!sp1_usb_midi_send(p+12,p[2]))break;
  __atomic_store_n(&peer.read,++r,__ATOMIC_RELEASE);
  if(mode==5){
   if(n==32&&md_valid(p,n,seed)){
    if(p[1]==MD_DATA&&p[2]&&!(p[2]%4)){
     if(!sp1_usb_midi_ready())continue;
     if(return_armed){unsigned key=irq_lock();for(unsigned j=0;j<p[2];j+=4)if((p[13+j]&240)==144&&p[15+j])bt_trace_add(&trace,k_uptime_get_32(),4,p[14+j]);irq_unlock(key);}
     p[1]=MD_ACK;p[2]=0;md_finish(p);send_frame(0x7e11,p,32);
    }else if(p[1]==MD_STATUS){printk("BTID329 MIDI %u %u %u ",p[3],p[28],p[29]);for(unsigned j=0;j<p[2];j++)printk("%02x",p[12+j]);printk("\n");}continue;
   }
   if(n==40)continue; /* Peer telemetry is not MIDI payload. */
   if(n!=24||p[0]!='M'||p[1]<2||p[1]>20||sp1_link_crc(p,22)!=((unsigned)p[22]|((unsigned)p[23]<<8))){++peer.invalid;continue;}
   ++peer.received;send_frame(0x7e11,p,24);++peer.sent;feedback(3);continue;
  }
  if(n==40&&!memcmp(p,"SPR1",4)&&sp1_link_crc(p,38)==((unsigned)p[38]|((unsigned)p[39]<<8))){
   if(p[4]!=1||p[5]!=mode-1||sp1_link_get(p+8)!=seed)continue; /* Pre-selection boot report. */
   peer.radio_state=p[6];if(p[6]==10){stop(-13);return;}
   bool live=p[6]==6||p[6]==7||p[6]==11;
   if(p[17]!=peer.disconnects){bt308_app_disconnect();peer.disconnects=p[17];peer.live=false;peer.proven=false;peer.client.remote_valid=0;memset(peer.client.remote_full,0,56);feedback(5);}
   if(live&&p[16]&&p[16]!=peer.generation){peer.generation=p[16];peer.live=true;peer.proven=false;
    if(!state_reconnect(&peer.client,seed+peer.generation,now)){stop(-16);return;}}
   if(!live){peer.live=false;peer.client.remote_valid=0;}
  }else if(n==40&&!memcmp(p,"SPT1",4)){/* Radio timing telemetry is not application data. */}
  else if(n==32&&p[0]=='S'&&p[1]=='L'&&p[2]==1&&sp1_link_crc(p,30)==((unsigned)p[30]|((unsigned)p[31]<<8))){if(peer.live){state_receive(&peer.client,p,now);++peer.received;}}
  else ++peer.invalid;
 }
 if(mode==5){midi_usb_service(now);return;}
 if(peer.live){uint8_t *state=peer.snapshot_scratch,out[32];bt308_snapshot(state);int v=state_poll_paced(&peer.client,now,state,true,out,mode==4?10:250);
  if(v<0||peer.client.errors||peer.client.link.invalid){stop(-18);return;}
  if(v){send_frame(0x7e10,out,32);++peer.sent;}
  bool proof=mode==4?peer.client.link.acknowledged>=RICH_PARTS:peer.client.remote_valid;
  if(proof&&!peer.proven){peer.proven=true;feedback(3);}
 }
}
/*402: cancel deferred command mutations after a storage fault, but keep the
 * established radio lifecycle/DMA serviced. Search state2 owns live diagnostic
 * scratch; resetting it would let another telemetry format overwrite it. */
static int fault402_gate_request(int r){
 if(!storage402_faulted())return r;
 pending=0;if(search_pending==1)search_pending=0;standalone_pending=false;
 unsigned key=irq_lock();if(mode==5&&phase==4)mq.pending=0;irq_unlock(key);
 return ((r>=91&&r<=97)||r==100||r==101||r==102)?r:0;
}
void sp1_bt_probe_service(bool allowed){
 sp1_usb_midi_poll();
 uint32_t now=k_uptime_get_32();int r=fault402_gate_request(atomic_set(&request,0));
 if(r==102){extern void scissors467_report(void);printk("ROCKER468 HELLO mode=runtime identity=%08x%08x\n",NRF_FICR->DEVICEID[1],NRF_FICR->DEVICEID[0]);scissors467_report();return;}
 if(r==101){printk("DRIVE464 HELLO mode=runtime identity=%08x%08x\n",NRF_FICR->DEVICEID[1],NRF_FICR->DEVICEID[0]);return;}
 if(r==100){printk("LIBRARY462 HELLO mode=runtime identity=%08x%08x\n",NRF_FICR->DEVICEID[1],NRF_FICR->DEVICEID[0]);return;}
 if(r==103){extern void storage523_report(void);storage523_report();return;}
 if(r==92){extern void storage373_report(void);storage373_report();return;}
 if(r==95){extern void storage400_report(void);storage400_report();return;}
 if(r==96){extern void timing401_report(void);timing401_report();return;}
 if(r==97){extern void power403_report(void);power403_report();return;}
 if(r==94){printk("BTID" CAPACITY_STRING(SP1_CAPACITY_BUILD) " RUNTIME identity=%08x%08x takes=%u\n",NRF_FICR->DEVICEID[1],NRF_FICR->DEVICEID[0],!storage523_legacy());return;}
 if(r==93){printk("BTID396 RUNTIME identity=%08x%08x comb=4\n",NRF_FICR->DEVICEID[1],NRF_FICR->DEVICEID[0]);return;}
 if(r==91){printk("BTID373 RUNTIME identity=%08x%08x setup=1\n",NRF_FICR->DEVICEID[1],NRF_FICR->DEVICEID[0]);return;}
 if(r==90||r==98||r==99||r==104){
  extern bool bt373_setup_safe(void),bt462_library_safe(void),bt464_drive_safe(void),bt535_radio_safe(void);
  if(r==104?!bt535_radio_safe():r==98?!bt464_drive_safe():r==99?!bt462_library_safe():(!allowed||!bt373_setup_safe())){printk("BTID373 SETUP REFUSED busy\n");return;}
  printk("BTID373 SETUP RESTART\n");
  k_msleep(250);NRF_POWER->GPREGRET2=r==104?0xa7u:r==99?0xaau:r==98?0xa8u:0xa6u;__DSB();sys_reboot(SYS_REBOOT_COLD);return;
 }
 if(r==42||r==43){
  if(!allowed||!bt308_stopped()||(unified[9]&1)||wedged){feedback(4);return;}
  /* Role changes only at a stopped boundary; never interrupt a live link. */
  stop(0);pending=r==42?7:6;search_pending=true;
  memset(audio_search,0,40);memset(audio_candidates,0,56);return;
 }
 if(search_pending==1&&phase==4&&active&&selected()){
  search_pending=2;uint8_t q[8]={'A','U','D','1'};sp1_link_put(q+4,seed);send_frame(0x7e21,q,sizeof q);
 }
 if(r>=50&&r<=57){
  if(phase==4&&!(unified[9]&1)&&bt308_stopped()){
   uint8_t q[9]={'S','E','L','1'};sp1_link_put(q+4,seed);q[8]=r-50;send_frame(0x7e22,q,sizeof q);
  }else feedback(4);
 }
 if(r==40){bt308_profile_arm();printk("BTID341 PROFILE armed playback-and-recording\n");return;}
 if(r==41){extern void bt341_profile_stop(void);bt341_profile_stop();printk("BTID341 PROFILE stopped\n");return;}
 /* Only a physical discovery gesture enables automatic exact-peer setup.
  * Serial SET/SRC commands retain the earlier explicit test behavior. */
 if(r==31){
  if(!allowed||!bt308_stopped()||phase!=4||mode!=6){feedback(4);return;}
  stop(0);pending=7;standalone_pending=true;return;
 }
 if(!r&&standalone_pending&&phase==4&&mode==7){
  if(!allowed||!bt308_stopped()||now-select_at>15000u){pending=0;stop(-33);return;}
  if(source_reports&&source_report[0]==0x31525356&&(source_report[1]==1||source_report[1]==2)&&source_report[2]==2){standalone_pending=false;r=30;}
 }

 if(r==30){
  if(!allowed||!bt308_stopped()||phase!=4||mode!=7||source_configured||!source_reports||source_report[0]!=0x31525356||source_report[1]!=1||source_report[2]!=2){++source_rejected;feedback(4);}
  else{
   source_route=source_pending[0];memcpy(source_target,source_pending+1,6);source_configured=true;
   uint8_t p[16]={'S','R','C','1',0,0,0,0,source_route,0};sp1_link_put(p+4,seed);memcpy(p+10,source_target,6);send_frame(0x7e20,p,sizeof p);
  }
 }

 if(cue_pending){if(!bt339_connect_safe())ui_at=now;else if(now-ui_at>=1000)cue_pending=false;} /* Defer each cue once, not on every later pause. */
 /* Request intake never clears in-flight audio/ISR memory. Unsafe requests
  * are refused rather than deferred until an unexpected later stop. */
 if(r==21){
  if(allowed&&bt308_stopped()&&phase==4&&!(unified[9]&1)){uint8_t clear[8]={'C','L','R','B',seed,seed>>8,seed>>16,seed>>24};send_frame(0x7e01,clear,8);}
  else feedback(4);
 }
 if(r==22){if(bt339_connect_safe()){view_open=false;cue_pending=false;}}
 if(r==20){
  if(!bt339_connect_safe())feedback(4);
  else if(!wedged){view_open=true;view_at=now;if(!mode&&!phase)r=16;
   else if(bt308_stopped()&&mode==1&&phase==4&&(unified[9]&1)&&(unified[9]&0x800u)&&!relay_report[2]){relay_report[2]=1;uint8_t p[8]={'R','L','Y','1'};sp1_link_put(p+4,seed);send_frame(0x7e06,p,8);feedback(1);}
  }
 }
 if(r>=10&&r<=17&&r-10==mode&&phase==4&&active&&selected()&&!wedged&&!errors&&!dma_bad)r=0; /* Reconnect to a live profile without destroying it. */
 if(r>=10&&r<=17)search_pending=false;
 if(r>=10&&r<=17){if(!((r==10||r==16)?bt339_connect_safe():(allowed&&bt308_stopped()))){feedback(4);}else if(!wedged){pending=r-10;if(phase&&phase!=5){bool disconnect=r==10;stop(0);if(disconnect)feedback(5);}}}
 if(phase==5){
  if(atomic_get(&owner)==1)atomic_cas(&owner,1,0);
  if(!bt_can_reuse(rx_enabled,atomic_get(&owner)==2)){
   if(now-finish_at>1000){wedged=true;pending=0;result=-8;reset(false);feedback(4);}return;
  }
  reset(false);NRF_P1->OUTCLR=2u;phase=0;done=true;mode=0;return;
 }
 if(!phase&&pending&&!wedged){
  if(!(pending==6?bt339_connect_safe():(allowed&&bt308_stopped()))){pending=0;feedback(4);return;}
  mode=pending;pending=0;result=0;begin_at=now;select_at=0;
  return_armed=false;return_quiet=0;memset(&uart_fault,0,sizeof uart_fault);rx_rearms=rx_rearm_wait=0;
  bytes=packets=audio_packets=audio_bytes=errors=discard=dma_bad=0;hn=left=status_n=status_reports=0;memset(unified,0,sizeof unified);memset(&trace,0,sizeof trace);
  input_blocks=playing_blocks=recording_blocks=record_audio_frames=monitor_max_us=0;memset(budgets,0,sizeof budgets);
  memset(relay_report,0,sizeof relay_report);relay_reports=0;source_configured=false;source_reports=0;source_rejected=0;memset(source_report,0,sizeof source_report);
  done=false;view_open=true;view_at=now;feedback(mode==3?2:1);
  if(rng_begin(now)<0){stop(-11);return;}phase=1;
 }
 if(!phase)return;
 if(!((mode==1||mode==6)&&bt308_return_safe()) && !midi_return_allowed(mode,phase,active,bt308_safe(),bt308_return_safe()) &&
    !companion_storage_allowed(mode,phase,active,bt308_companion_storage())){pending=0;stop(-3);return;}
 if(phase==1){int v=rng_poll(now);if(v<0){stop(-11);return;}if(!v)return;
  /* RNG no longer touches this union. RX/render still disabled. */
  memset(&bt_workspace,0,sizeof bt_workspace);audio_decode_reset(1192);if(bt308_stopped())bt308_profile_arm();
  if(!initialized){
   ownership(1);
   if(NRF_UARTE0->ENABLE||NRF_P1->PIN_CNF[5]!=2u||(NRF_P1->OUT&32u)){stop(-4);return;}
   bool retained=source_retained_safe(NRF_P0->PIN_CNF[10],NRF_P1->PIN_CNF[1],NRF_P0->OUT&1024u,NRF_P1->OUT&2u);
   if(((NRF_P0->PIN_CNF[10]|NRF_P1->PIN_CNF[1])&1u)&&!retained){stop(-4);return;}
   reset(false);NRF_UARTE0->PSEL.RTS=0xffffffffu;
   if(device_init(radio)<0||!device_is_ready(radio)){stop(-5);return;}initialized=true;
  }
  reset(false);
  if(NRF_UARTE0->PSEL.RTS!=0xffffffffu||(NRF_UARTE0->CONFIG&1u)||uart_callback_set(radio,rx_event,0)<0){stop(-6);return;}
  NRF_P1->OUTSET=2u;NRF_P1->PIN_CNF[1]=1u;dma_busy[0]=true;dma_busy[1]=false;rx_enabled=true;
  /* Infinite DMA timeout preserves the tested audio path. Small control
   * frames use a bounded idle flush so they never wait for 256 bytes. */
  if(uart_rx_enable(radio,dma_buf[0],256,mode==1?SYS_FOREVER_US:2000)<0){rx_enabled=false;stop(-7);return;}
  if(mode==1)NRF_UARTE0->INTENCLR=UARTE_INTENCLR_RXDRDY_Msk;
  begin_at=now;phase=2;active=true;atomic_set(&owner,mode==1?1:0);armed_print=true;++switches;
 }
 if(phase==6){
  if(rx_enabled){if(now-select_at>1000)stop(-8);return;}
  /* Radio withholds payload until acknowledgement; preserve the connection. */
  mode=handoff;handoff=0;memset(&bt_workspace,0,sizeof bt_workspace);
  audio_decode_reset(1192);hn=left=payload_n=status_n=0;dma_busy[0]=true;dma_busy[1]=false;
  rx_enabled=true;
  if(uart_rx_enable(radio,dma_buf[0],256,mode==1?SYS_FOREVER_US:2000)<0){rx_enabled=false;stop(-7);return;}
  if(mode==1)NRF_UARTE0->INTENCLR=UARTE_INTENCLR_RXDRDY_Msk;
  atomic_set(&owner,mode==1?1:0);select_mode();select_at=now;phase=3;begin_at=now;return;
 }
 /* Only an isolated framing fault at an already-open outgoing AVDTP channel
  * may rearm RX. Never suppress errors on incoming audio/recording paths. */
 if(rx_rearm_wait==1){
  if(errors!=UART_ERROR_FRAMING||dma_bad||now-rx_rearm_at>100){stop(-8);return;}
  if(rx_enabled||dma_busy[0]||dma_busy[1])return;
  hn=left=payload_n=status_n=0;errors=0;dma_busy[0]=true;rx_enabled=true;
  if(uart_rx_enable(radio,dma_buf[0],256,2000)<0){rx_enabled=false;stop(-8);return;}
  rx_rearm_wait=2;rx_rearm_reports=status_reports;rx_rearm_at=now;return;
 }
 if(rx_rearm_wait==2){
  if(!rx_enabled||dma_bad||errors||now-rx_rearm_at>2000){stop(-8);return;}
  if(status_reports==rx_rearm_reports)return;
  if(!selected()){stop(-8);return;}rx_rearm_wait=0;
 }
 if(!rx_enabled||dma_bad||errors){
  if(mode==7&&phase==4&&!rx_rearms&&!dma_bad&&errors==UART_ERROR_FRAMING&&
     source_report[1]==2&&source_report[2]==6&&!source_report[3]&&
     !left&&!uart_fault.rx_len){
   rx_rearms=1;rx_rearm_wait=1;rx_rearm_at=now;return;
  }
  stop(-8);return;
 }
 if(phase==2&&now-begin_at>=100){reset(true);phase=3;}
 if(phase==3&&now-begin_at>=150)NRF_P1->OUTCLR=2u;
 if(phase==3||phase==4){
  if(mode==6&&unified[3]>=1&&unified[3]<=5&&unified[4]==seed&&!unified[5]&&!unified[6]){
   handoff=unified[3];select_at=now;phase=6;
   if(uart_rx_disable(radio)<0){wedged=true;stop(-8);}return;
  }
 }
 if(phase==3){
  if(selected()&&(mode==6||unified[8])){phase=4;select_at=now;}
  else if(status_reports&&unified[0]==0x31555053&&unified[1]==2&&unified[2]==1&&!unified[5]&&now-begin_at>=1000&&now-select_at>=500){
   select_mode();select_at=now;
  }
  if((status_reports&&(unified[5]||unified[6]))||now-begin_at>20000){stop(-30);return;}
 }
 if(phase==4){
  if(!selected()){stop(-31);return;}
  if(mode==1&&relay_report[2]==1&&!relay_reports&&now-view_at>5000){relay_report[2]=4;relay_report[3]=0xffff;}
  if(mode==1||mode==7){if((relay_report[2]==1||relay_report[2]==2))feedback(1);else if(relay_report[2]==4)feedback(4);else if(mode==7?sp1_bt_output_live():(unified[9]&1))feedback(3);else if(ui==3)feedback(5);else if(ui!=5||!cue_pending)feedback(1);}
  else if(mode==6){if(now-begin_at>120000){pending=0;stop(-32);}}
  else {const uint8_t *reply;unsigned n=bt308_app_reply(&reply);if(mode==4&&n){send_frame(0x7e30,reply,n);if(n>=14&&reply[6]==2&&!reply[7])feedback(3);bt308_app_sent();}peer_service(now);}
 }
}
bool bt308_monitor(int16_t*out,unsigned frames,unsigned gain,unsigned usb_frames){
 block_stage=0;
 if(!atomic_cas(&owner,1,2))goto fallback;
 if(source_configured||!active||mode!=1||!bt308_return_safe()||!(unified[9]&1)){if(mode==1)audio_decode_discard();atomic_set(&owner,1);goto fallback;}
 if(!input_selected&&!bt339_input_switch_safe()){audio_decode_discard();atomic_set(&owner,1);return false;}
 if(usb_frames<frames)memset(out+2*usb_frames,0,(frames-usb_frames)*4);
 uint32_t cycles=DWT->CYCCNT;unsigned before=audio_decode_stats.output,context=bt308_context();
 block_stage=(context&2)?3:(context&4)?4:(context&1)?2:1;
 ++input_blocks;if(context&1)++playing_blocks;if(context&2)++recording_blocks;
#ifdef CONFIG_SP1_LINK_CALIBRATION
 extern void bt315_cal_input(const int16_t*,unsigned,bool);
 unsigned cal_gaps=audio_decode_stats.gap_count,cal_errors=audio_decode_stats.errors,cal_overflow=audio_decode_stats.overflow;
#endif
 audio_decode_mix(out,frames,gain,&input_selected,bt340_usb_input());
#ifdef CONFIG_SP1_LINK_CALIBRATION
 bt315_cal_input(out,frames,audio_decode_rate()==48000&&audio_decode_stats.output-before==frames&&audio_decode_stats.gap_count==cal_gaps&&audio_decode_stats.errors==cal_errors&&audio_decode_stats.overflow==cal_overflow);
#endif
 if(context&0xf0000u)record_audio_frames+=audio_decode_stats.output-before;
 unsigned us=(DWT->CYCCNT-cycles)/64;if(us>monitor_max_us)monitor_max_us=us;
 input_last_l=out[2*(frames-1)];input_last_r=out[2*(frames-1)+1];
 atomic_set(&owner,1);return input_selected || audio_decode_stats.output>before;
fallback:
 if(!input_selected)return false;
 /* USB was already part of the live mix: keep it audible during BT loss, including a take. */
 if(usb_frames<frames)memset(out+2*usb_frames,0,(frames-usb_frames)*4);
 for(unsigned i=0;i<frames&&i<64;i++){out[2*i]=(input_last_l*(int32_t)(64-i)+(int32_t)out[2*i]*i)/64;out[2*i+1]=(input_last_r*(int32_t)(64-i)+(int32_t)out[2*i+1]*i)/64;}
 input_selected=false;return true;
}
unsigned bt339_input_state(void){return input_selected?(bt340_usb_input()?3u:1u):(active&&mode==1&&!bt339_input_switch_safe()?2u:0u);}
void bt308_budget(unsigned us){unsigned i=block_stage;if(i>4)i=0;++budgets[i].blocks;if(us>=5333)++budgets[i].over;if(us>budgets[i].max_us)budgets[i].max_us=us;block_stage=0;}
#ifdef CONFIG_SP1_LINK_CALIBRATION
uint32_t bt315_cal_session(void){return seed;}
uint32_t bt315_cal_faults(void){return audio_decode_stats.errors+audio_decode_stats.overflow+audio_decode_stats.gap_count;}
unsigned bt315_cal_role(void){
 if(!active||phase!=4||!(unified[9]&2)||!(unified[9]&1))return 0;
 return mode==7?1:mode==1&&!source_configured?2:0;
}
#endif
/*402: exact read-query whitelist shared by normal and fault RX. The existing
 *25-byte command buffer is reused; no transfer/app parser or new RAM buffer. */
static bool probe_read_query(void){
 if(cn==9&&!memcmp(command,"A395INFO?",9)){atomic_set(&info_request,2);return true;}
 if(cn==9&&!memcmp(command,"A269INFO?",9)){atomic_set(&info_request,1);return true;}
 int r=0;
 if(cn==9&&!memcmp(command,"A523MODE?",9))r=103;
 if(cn==10&&!memcmp(command,"A468HELLO?",10))r=102;
 if(cn==10&&!memcmp(command,"A464HELLO?",10))r=101;
 if(cn==10&&!memcmp(command,"A373CHECK?",10))r=92;
 if(cn==10&&!memcmp(command,"A396HELLO?",10))r=93;
 if(cn==8&&(!memcmp(command,"A402ERR?",8)||!memcmp(command,"A401ERR?",8)))r=95;
 if(cn==9&&(!memcmp(command,"A402TIME?",9)||!memcmp(command,"A401TIME?",9)))r=96;
 if(cn==10&&(!memcmp(command,"A" CAPACITY_STRING(SP1_CAPACITY_BUILD) "HELLO?",10)||!memcmp(command,"A403HELLO?",10)||!memcmp(command,"A402HELLO?",10)||!memcmp(command,"A401HELLO?",10)||!memcmp(command,"A397HELLO?",10)))r=94;
 if(cn==10&&!memcmp(command,"A373HELLO?",10))r=91;
 if(cn==10&&!memcmp(command,"A403POWER?",10))r=97;
 if(!r)return false;atomic_set(&request,r);return true;
}
void sp1_bt_probe_fault_enter(void){
 /* Streamer owns RX framing. Discard a partial pre-fault command and any
  * queued command before the fault-only dispatcher starts. */
 cn=0;atomic_set(&request,0);
}
static void probe_command_byte(uint8_t b,bool read_only){
 if(b=='A')cn=0;if(cn>=sizeof command)cn=0;command[cn++]=b;if(b!='?')return;
 if(probe_read_query()||read_only){cn=0;return;}
 if(cn==21&&!memcmp(command,"A269SRC",7)){
  uint8_t parsed[7];
  if(!atomic_get(&request)&&video_parse_command(command,cn,parsed)){memcpy(source_pending,parsed,7);atomic_set(&request,30);}else ++source_rejected;
 }
 if(cn==24&&!memcmp(command,"A286MID",7)&&mode==5&&phase==4){
  uint8_t q[8];bool valid=true;for(unsigned i=0;i<16;i++){char c=command[7+i];unsigned v=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:255;if(v>15){valid=false;break;}if(!(i&1))q[i/2]=v<<4;else q[i/2]|=v;}
  unsigned k=irq_lock();if(valid&&mode==5&&phase==4&&!mq.pending){memcpy(mq.control,q,8);mq.pending=1;}irq_unlock(k);
 }
 if(cn==8&&!memcmp(command,"A295END?",8)){unsigned key=irq_lock();return_armed=false;irq_unlock(key);}
 if(cn==8&&!memcmp(command,"A295RET?",8)&&mode==5&&phase==4){unsigned key=irq_lock();memset(&trace,0,sizeof trace);return_quiet=0;return_armed=true;irq_unlock(key);}
#ifdef CONFIG_SP1_LINK_CALIBRATION
 if(cn==9&&!memcmp(command,"A315TIM",7)&&command[7]>='1'&&command[7]<='2'){
  extern void bt315_cal_request(unsigned);bt315_cal_request(command[7]-'0');
 }
#endif
 if(cn==8&&!memcmp(command,"A395OUT?",8))atomic_set(&request,42);
 if(cn==7&&!memcmp(command,"A395IN?",7))atomic_set(&request,43);
 if(cn==9&&!memcmp(command,"A395SEL",7)&&command[7]>='0'&&command[7]<='7')atomic_set(&request,50+command[7]-'0');
 if(cn==9&&!memcmp(command,"A341PROF?",9))atomic_set(&request,40);
 if(cn==9&&!memcmp(command,"A341STOP?",9))atomic_set(&request,41);
 if(cn==9&&!memcmp(command,"A539BTUP?",9))atomic_set(&request,104);
 if(cn==10&&!memcmp(command,"A373SETUP?",10))atomic_set(&request,90);
 if(cn==8&&!memcmp(command,"A462LIB?",8))atomic_set(&request,99);
 if(cn==10&&!memcmp(command,"A462HELLO?",10))atomic_set(&request,100);
 if(cn==9&&!memcmp(command,"A269SET",7)&&command[7]>='0'&&command[7]<='7')atomic_set(&request,10+command[7]-'0');
 if(cn==9&&!memcmp(command,"A269WIPE?",9))atomic_set(&request,21);
 cn=0;
}
void sp1_bt_probe_byte(uint8_t b){probe_command_byte(b,storage402_faulted());}
void sp1_bt_probe_fault_byte(uint8_t b){probe_command_byte(b,true);}
void sp1_bt_probe_report(void){
 extern bool bt308_app_active(void);if(bt308_app_active()&&!storage402_faulted())return;
 uint32_t dtr=0;uart_line_ctrl_get(console,UART_LINE_CTRL_DTR,&dtr);if(!dtr)return;
 if(!announced){
#ifdef CONFIG_SP1_LINK_CALIBRATION
  printk("BTID315 READY stopped-device-calibration; diagnostics retain BTID329 labels\n");
#else
  printk("BTID329 READY unified-controller radio-auto-v2\n");
#endif
  announced=true;
 }
 if(armed_print){printk("BTID329 ARMED mode=%u nonce=%u\n",mode,seed);armed_print=false;}
 int report_kind=atomic_set(&info_request,0);if(!report_kind&&!done)return;done=false;
 if(!report_kind&&audio_search[3]==1)report_kind=2;
 /* Discovery diagnostics share the explicit INFO gate; never print from idle polling. */
 if(search_pending==2&&audio_search[0]==0x31444341){
 printk("BTID396 SEARCH state=%u direction=%u count=%u cursor=%u error=%u overflow=%u uncertain=%u seconds=%u\n",audio_search[2],audio_search[3],audio_search[4],audio_search[5],audio_search[6],audio_search[7],audio_search[8],audio_search[9]);
 for(unsigned i=0;i<audio_search[4]&&i<8&&(mode==1||mode==6||mode==7);i++){printk("BTID396 CANDIDATE index=%u compatible=%u address=",i,audio_candidates[i][0]);for(unsigned j=1;j<7;j++)printk("%02x",audio_candidates[i][j]);printk("\n");}
 }else printk("BTID396 SEARCH state=0 direction=0 count=0 cursor=0 error=0 overflow=0 uncertain=0 seconds=0\n");
 if(report_kind==2){
 printk("BTID396 UART_RECOVERY attempts=%u waiting=%u errors=%u faults=%u reason=%u dma_bad=%u rx=%u\n",rx_rearms,rx_rearm_wait,errors,uart_fault.count,uart_fault.reason,dma_bad,rx_enabled);
 printk("BTID396 UART_FAULT ms=%u opcode=%u remaining=%u rx_len=%u state=%u hfclk=%08x pins=%08x amount=%u\n",uart_fault.ms,uart_fault.position>>16,uart_fault.position&65535u,uart_fault.rx_len,uart_fault.state,uart_fault.hfclk,uart_fault.pins,uart_fault.rx_amount);
 printk("BTID396 RADIO mode=%u phase=%u active=%u result=%d connected=%u streaming=%u\n",mode,phase,active,result,unified[9]&1u,(unified[9]>>1)&1u);
 printk("BTID396 SOURCE version=%u phase=%u error=%u events=%u route=%u seid=%u endpoint=%u total=%u lcid=%u mtu=%u\n",source_report[1],source_report[2],source_report[3],source_report[4],source_report[5]&255u,(source_report[5]>>8)&255u,(source_report[5]>>16)&255u,source_report[5]>>24,source_report[6],source_report[7]);
 if(source_report[1]==2||source_report[1]==3){printk("BTID396 CODEC raw=");for(unsigned i=32;i<40;i++)printk("%02x",((uint8_t*)source_report)[i]);printk("\n");}
 if(source_report[1]==4){printk("BTID396 RECEIVER name8_hex=");for(unsigned i=32;i<40;i++)printk("%02x",((uint8_t*)source_report)[i]);printk("\n");}
 extern int bt308_headphones(void);
 printk("BTID396 OUTPUT live=%u speaker_expected=%u headphones=%d\n",sp1_bt_output_live(),!sp1_bt_output_live()&&bt308_headphones()!=1,bt308_headphones());
 printk("BTID396 END\n");return;
 }
 ownership(0);
#ifdef CONFIG_SP1_LINK_CALIBRATION
 printk("BTID315 CAL capability=1\n");
#endif
 bt308_return_report();
 uint8_t identity[56];bt308_snapshot_base(identity);printk("BTID329 ID ");for(unsigned i=48;i<56;i++)printk("%02x",identity[i]);printk("\n");
 printk("BTID329 SNAP data=");for(unsigned i=0;i<56;i++)printk("%02x",identity[i]);printk("\n");
 printk("BTID329 STATUS mode=%u phase=%u active=%u result=%d selected=%u nonce=%u switches=%u wedged=%u context=%u\n",mode,phase,active,result,selected(),seed,switches,wedged,bt308_context());
 printk("BTID329 UART bytes=%u packets=%u audio_packets=%u audio_bytes=%u errors=%u discarded=%u bad=%u partial=%u\n",bytes,packets,audio_packets,audio_bytes,errors,discard,dma_bad,left);
 printk("BTID329 UART_FAULT count=%u first_ms=%u reason_mask=%u opcode=%u remaining=%u radio_tick=%u rx_len=%u state=%u\n",uart_fault.count,uart_fault.ms,uart_fault.reason,uart_fault.position>>16,uart_fault.position&0xffffu,uart_fault.radio_tick,uart_fault.rx_len,uart_fault.state);
 printk("BTID329 UART_HW hfclk=%08x p1_in=%08x rx_amount=%u tx_amount=%u\n",uart_fault.hfclk,uart_fault.pins,uart_fault.rx_amount,uart_fault.tx_amount);
 printk("BTID329 RADIO reports=%u mode=%u nonce=%u failed=%u rejected=%u tick=%u armed=%u connected=%u streaming=%u bond_flags=%u\n",status_reports,unified[3],unified[4],unified[5],unified[6],unified[7],unified[8],unified[9]&1,(unified[9]>>1)&1,unified[9]&0xf0);
 printk("BTID329 AUDIO frames=%u errors=%u overflow=%u output=%u gaps=%u gap_max=%u input=%u playing=%u recording=%u recorded=%u monitor_max_us=%u i2s_errors=%u block_max_us=%u\n",audio_decode_stats.frames,audio_decode_stats.errors,audio_decode_stats.overflow,audio_decode_stats.output,audio_decode_stats.gap_count,audio_decode_stats.gap_max,input_blocks,playing_blocks,recording_blocks,record_audio_frames,monitor_max_us,bt308_output_errors(),bt308_output_max_us());
 for(unsigned i=0;i<5;i++)printk("BTID329 BUDGET stage=%u blocks=%u max_us=%u over=%u\n",i,budgets[i].blocks,budgets[i].max_us,budgets[i].over);
 if(mode>=2&&phase>=2&&phase<=4)printk("BTID329 PEER generation=%u state=%u live=%u sent=%u received=%u invalid=%u snapshots_tx=%u snapshots_rx=%u acknowledged=%u errors=%u disconnects=%u\n",peer.generation,peer.radio_state,peer.live,peer.sent,peer.received,peer.invalid,peer.client.full_sent,peer.client.full_received,peer.client.link.acknowledged,peer.client.errors,peer.disconnects);
 if(mode>=2&&mode<=4&&phase>=2&&phase<=4){printk("BTID329 REMOTE ");for(unsigned i=48;i<56;i++)printk("%02x",peer.client.remote_full[i]);printk("\n");}
 bt308_detail_report();
 printk("BTID329 RECORD overruns=%u stored_glitches=%u\n",bt308_record_health(0),bt308_record_health(1));
 unsigned key=irq_lock();unsigned count=trace.count,dropped=trace.dropped;irq_unlock(key);
 printk("BTID329 TRACE count=%u dropped=%u now_ms=%u open_samples=%u\n",count,dropped,k_uptime_get_32(),audio_decode_stats.gap_open);
 for(unsigned i=0;i<count;i++){
  key=irq_lock();struct bt_trace_event event=trace.events[i];irq_unlock(key);
  printk("BTID329 EVENT index=%u ms=%u kind=%u value=%u\n",i,event.ms,event.kind,event.value);
 }
 printk("BTID329 RELAY reports=%u state=%u error=%u flags=%u media=%u forwarded=%u failed=%u invalid=%u\n",relay_reports,relay_report[2],relay_report[3],relay_report[5],relay_report[6],relay_report[7],relay_report[8],relay_report[9]);
 printk("BTID329 SOURCE reports=%u magic=%u version=%u phase=%u error=%u events=%u route=%u lcid=%u mtu=%u min_free=%u tick=%u configured=%u rejected=%u target=",source_reports,source_report[0],source_report[1],source_report[2],source_report[3],source_report[4],source_report[5],source_report[6],source_report[7],source_report[8],source_report[9],source_configured,source_rejected);for(unsigned i=0;i<6;i++)printk("%02x",source_target[i]);printk("\n");
 printk("BTID329 DISCOVERY reports=%u bad=%u data=",discovery_reports,discovery_bad);for(unsigned i=0;i<40;i++)printk("%02x",discovery_report[i]);printk("\n");
 printk("BTID329 END\n");
}
