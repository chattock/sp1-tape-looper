#include "release_policy.h"
/* Experimental automatic discovery. One foreground function, controller-armed forwarding. */
#include <string.h>
#include "sparcommon.h"
#include "app_bt_cfg.h"
#include "wiced_bt_stack.h"
#include "wiced_bt_dev.h"
#include "wiced_bt_ble.h"
#include "wiced_transport.h"
#include "wiced_bt_trace.h"
#include "wiced_timer.h"
#include "mode_gate.h"
#include "auto.h"
#include "bond_store.h"

extern const wiced_bt_cfg_buf_pool_t wiced_bt_cfg_buf_pools[];
extern wiced_result_t sp1_audio_start(void),sp1_midi_start(void),sp1_peer_start(unsigned,uint32_t),sp1_auto_start(uint32_t,const uint8_t*);
extern wiced_result_t sp1_audio_management(wiced_bt_management_evt_t,wiced_bt_management_evt_data_t*);
extern void sp1_app_response(uint8_t*,unsigned);
extern void sp1_peer_input(uint8_t*,uint32_t),sp1_midi_input(uint8_t*,uint32_t);
extern wiced_result_t sp1_source_start(void);
extern int sp1_source_connect(const uint8_t*,unsigned,uint32_t);
static struct sp1_mode_gate gate;
extern int sp1_discovery_start(unsigned),sp1_discovery_select(unsigned);
extern void sp1_discovery_status(void);
extern wiced_result_t wiced_bt_a2dp_sink_connect(uint8_t*);
uint32_t sp1_midi_seed(void){return gate.nonce;}
static uint8_t relay_state; /* 0 inactive,1 searching,2 connecting,3 streaming,4 failed */
static uint32_t relay_error;
extern int sp1_relay_discover(void);
extern int sp1_audio_is_peer(void);

static uint32_t rejected,ticks,flags;static uint8_t automatic,armed,identity[8];static wiced_timer_t timer;
static void report(uint32_t unused){(void)unused;uint32_t r[10]={0x31555053,2,gate.ready,gate.mode,gate.nonce,gate.failed,rejected,++ticks,armed,flags|bond_flags};wiced_transport_send_data(0x7e00,(uint8_t*)r,sizeof r);}
extern void sp1_audio_media_state(uint32_t*);
static void relay_report(void){uint32_t r[10]={0x31594c52,1,relay_state,relay_error,gate.nonce,flags};sp1_audio_media_state(r+6);wiced_transport_send_data(0x7e06,(uint8_t*)r,sizeof r);}
int sp1_relay_active(void){return relay_state!=0;}
void sp1_relay_search_done(unsigned reason){relay_state=reason?4:2;relay_error=reason;relay_report();}
int sp1_relay_begin(void){
 return 0;
}
void sp1_source_connection(unsigned connected,unsigned streaming){
 (void)connected;(void)streaming;
}
int sp1_auto_enabled(void){return automatic;}
unsigned sp1_active_mode(void){return gate.mode;}
int sp1_forward_ready(unsigned mode){return gate.ready&&!gate.failed&&armed&&gate.mode==mode;}
extern int sp1_election_busy(void);
int sp1_claim(unsigned mode){
 if(gate.mode!=mode&&sp1_election_busy())return 0;
 if(!gate.ready||gate.failed||!sr_claim(mode))return 0;
 if(gate.mode==mode)return 1;
 if(!automatic||gate.mode!=6)return 0;
 gate.mode=mode;sp1_auto_discovery_stop(mode);report(0);return 1;
}
void sp1_connection(unsigned connected,unsigned streaming){flags=(flags&~3u)|(connected?1:0)|(streaming?2:0);report(0);}
void sp1_source_handoff(const uint8_t *address){
 (void)address;
}
static uint32_t receive(uint8_t*p,uint32_t n){
 unsigned bad=0;if(!p)return WICED_BT_ERROR;
 if(n<4||sp1_u16(p+2)!=n-4)bad=1;
 else if(sp1_u16(p)==0x7e06){
  bad=1;++rejected;
 }else if(sp1_u16(p)==0x7e01){
  if(n!=12||memcmp(p+4,"CLRB",4)||sp1_u32(p+8)!=gate.nonce||!gate.mode||(flags&1)||!bond_clear())++rejected;
  report(0);
 }else if(sp1_u16(p)==0x7e00){
  uint8_t*q=p+4;
  if(n!=24||memcmp(q,"SPU2",4)||q[4]!=2||!sr_select(q[5])||q[6]||q[7]||!sp1_u32(q+8)||!gate.ready||gate.failed)bad=1;
  else if(gate.mode){
   if(gate.nonce!=sp1_u32(q+8)||memcmp(identity,q+12,8))bad=1;
   else if(gate.mode==q[5])armed=gate.mode!=6;
   else if(!(automatic&&!armed&&q[5]==6))bad=1;
  }else{
   gate.mode=q[5];gate.nonce=sp1_u32(q+8);memcpy(identity,q+12,8);automatic=gate.mode==6;armed=!automatic;
   wiced_result_t rc;
   if(automatic){rc=sp1_auto_start(gate.nonce,identity);if(rc==WICED_BT_SUCCESS)rc=sp1_audio_start();}
   else if(gate.mode==7)rc=sp1_source_start();
   else rc=gate.mode==1?sp1_audio_start():sp1_peer_start(3,gate.nonce);
   if(rc!=WICED_BT_SUCCESS){gate.failed=1;bad=1;}
  }
  if(bad)++rejected;report(0);
 }else if(sp1_u16(p)==0x7e21){
  if(n!=12||memcmp(p+4,"AUD1",4)||sp1_u32(p+8)!=gate.nonce||(flags&1)||!(gate.mode==1||gate.mode==6||gate.mode==7)||!sp1_discovery_start(gate.mode==7)){bad=1;++rejected;}
 }else if(sp1_u16(p)==0x7e22){
  if(n!=13||memcmp(p+4,"SEL1",4)||sp1_u32(p+8)!=gate.nonce||(flags&1)||!sp1_discovery_select(p[12])){bad=1;++rejected;}
 }else if(sp1_u16(p)==0x7e23){sp1_discovery_status();
 }else if(sp1_u16(p)==0x7e20&&gate.mode==7&&armed){if(!sp1_source_connect(p+4,n-4,gate.nonce)){bad=1;++rejected;}
 }else if(sp1_u16(p)==0x7e30&&gate.mode==4&&armed){sp1_app_response(p+4,n-4);
 }else if(armed&&sp1_channel(&gate,sp1_u16(p),n-4)){
  sp1_peer_input(p+4,n-4);
 }else {bad=1;++rejected;}
 wiced_transport_free_buffer(p);return bad?WICED_BT_ERROR:WICED_SUCCESS;
}
static const wiced_transport_cfg_t transport={.type=WICED_TRANSPORT_UART,.cfg.uart_cfg={.mode=WICED_TRANSPORT_UART_HCI_MODE,.baud_rate=1000000},.rx_buff_pool_cfg={.buffer_size=1024,.buffer_count=2},.p_data_handler=receive};
static wiced_result_t management(wiced_bt_management_evt_t e,wiced_bt_management_evt_data_t*d){
 /* Recorder pairing must not overwrite the single persistent phone bond.
  * Relay output is deliberately session-only; a fresh session pairs again. */
 if(gate.mode==7&&e==BTM_PAIRED_DEVICE_LINK_KEYS_UPDATE_EVT)return WICED_BT_SUCCESS;
 if(gate.mode==7&&e==BTM_PAIRED_DEVICE_LINK_KEYS_REQUEST_EVT)return WICED_BT_ERROR;
 if(e==BTM_PAIRED_DEVICE_LINK_KEYS_UPDATE_EVT||e==BTM_PAIRED_DEVICE_LINK_KEYS_REQUEST_EVT||e==BTM_LOCAL_IDENTITY_KEYS_UPDATE_EVT||e==BTM_LOCAL_IDENTITY_KEYS_REQUEST_EVT)return bond_event(e,d);
 if(e==BTM_ENABLED_EVT){gate.ready=d->enabled.status==WICED_BT_SUCCESS;report(0);return gate.ready?WICED_BT_SUCCESS:WICED_BT_ERROR;}
 if(automatic||gate.mode==1||gate.mode==7)return sp1_audio_management(e,d);
 if(e==BTM_PAIRED_DEVICE_LINK_KEYS_REQUEST_EVT||e==BTM_LOCAL_IDENTITY_KEYS_REQUEST_EVT)return WICED_BT_ERROR;
 return WICED_BT_SUCCESS;
}
static void resource_tick(uint32_t unused){(void)unused;report(0);if(relay_state)relay_report();}
APPLICATION_START(){wiced_set_debug_uart(WICED_ROUTE_DEBUG_NONE);if(wiced_transport_init(&transport)!=WICED_SUCCESS)return;wiced_init_timer(&timer,resource_tick,0,WICED_SECONDS_PERIODIC_TIMER);wiced_start_timer(&timer,1);if(wiced_bt_stack_init(management,&wiced_bt_cfg_settings,wiced_bt_cfg_buf_pools)!=WICED_BT_SUCCESS)gate.failed=1;}

int sp1_source_peer(const uint8_t *a){(void)a;return 0;}

int sp1_discovery_connect(const uint8_t *a,unsigned outgoing){
 if(flags&1)return 0;
 if(outgoing){
  if(gate.mode!=7||!armed)return 0;
  uint8_t q[16]={'S','R','C','1'};memcpy(q+4,&gate.nonce,4);memcpy(q+10,a,6);
  return sp1_source_connect(q,sizeof q,gate.nonce);
 }
 if(gate.mode!=1&&gate.mode!=6)return 0;
 return wiced_bt_a2dp_sink_connect((uint8_t*)a)==WICED_BT_SUCCESS;
}
