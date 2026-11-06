#include "device_name.h"
#include "bond_policy.h"
#include "release_policy.h"
/* Persistent UART/BLE relay candidate. Idle until a CRC-checked UART command.
 * No bonding, NVRAM, flash programming, board GPIO or audio. */
#include <string.h>
#include "auto.h"
#include "../auto-role-core/election.h"
#include "app_bt_cfg.h"
#include "sparcommon.h"
#include "wiced_bt_stack.h"
#include "wiced_bt_ble.h"
#include "wiced_bt_dev.h"
#include "wiced_bt_gatt.h"
#include "wiced_bt_l2c.h"
#include "wiced_bt_uuid.h"
#include "wiced_bt_trace.h"
#include "wiced_timer.h"
#include "wiced_transport.h"
#include "../peer-protocol/packet.h"
#include "../persistent-transport/bridge.h"

extern wiced_bt_gatt_status_t sp1_midi_gatt(wiced_bt_gatt_evt_t,wiced_bt_gatt_event_data_t*);
static struct election election;static uint32_t election_ad_at;static uint8_t election_internal;
static uint8_t auto_on,auto_advert_on;static uint32_t discovery_at;
int sp1_election_busy(void){
 return 0;
}
int sp1_election_audio_peer(const uint8_t *a){
 (void)a;return 1;
}
int sp1_election_reserved(void){
 return 0;
}
static uint8_t relay_search;
extern void sp1_source_handoff(const uint8_t*);
static sp1_link_bridge relay;
static uint8_t subscribed,role;
/* Companion deliberately stays at 20-byte SF framing regardless of MTU. */
static uint8_t poll_write,fast_mode,service_busy,update_sent;
static uint16_t mtu=23,period_ms=50;static uint8_t mtu_seen;static uint32_t report_at,timing_at;
#define selected_mode 5u
static unsigned packet_bytes(void){return role!=3&&(selected_mode&1)&&mtu>=35?32:20;}
extern const wiced_bt_cfg_buf_pool_t wiced_bt_cfg_buf_pools[];
#define SERVICE_UUID 0x72,0x91,0x64,0xf8,0xf0,0x12,0x46,0xa9,0x88,0xce,0x14,0x60,0x01,0x00,0x50,0x53
#define VALUE_UUID   0x72,0x91,0x64,0xf8,0xf0,0x12,0x46,0xa9,0x88,0xce,0x14,0x60,0x02,0x00,0x50,0x53
#define COMPANION_UUID 0x72,0x91,0x64,0xf8,0xf0,0x12,0x46,0xa9,0x88,0xce,0x14,0x60,0x03,0x00,0x50,0x53
#define STREAM_UUID 0x72,0x91,0x64,0xf8,0xf0,0x12,0x46,0xa9,0x88,0xce,0x14,0x60,0x04,0x00,0x50,0x53
#define LINK_UUID 0x72,0x91,0x64,0xf8,0xf0,0x12,0x46,0xa9,0x88,0xce,0x14,0x60,0x07,0x00,0x50,0x53
#define APP_UUID 0x72,0x91,0x64,0xf8,0xf0,0x12,0x46,0xa9,0x88,0xce,0x14,0x60,0x06,0x00,0x50,0x53
#define CAP_UUID 0x72,0x91,0x64,0xf8,0xf0,0x12,0x46,0xa9,0x88,0xce,0x14,0x60,0x05,0x00,0x50,0x53
static uint8_t companion_uuid[]={COMPANION_UUID};
static uint8_t service_uuid[]={SERVICE_UUID}, value_uuid[]={VALUE_UUID};


static const uint8_t database[]={
 PRIMARY_SERVICE_UUID16(1,UUID_SERVICE_GAP),
 CHARACTERISTIC_UUID16(2,3,GATT_UUID_GAP_DEVICE_NAME,GATTDB_CHAR_PROP_READ,GATTDB_PERM_READABLE),
 PRIMARY_SERVICE_UUID16(4,UUID_SERVICE_GATT),
 PRIMARY_SERVICE_UUID128(0x20,COMPANION_UUID),
 CHARACTERISTIC_UUID128_WRITABLE(0x21,0x22,STREAM_UUID,GATTDB_CHAR_PROP_NOTIFY|GATTDB_CHAR_PROP_WRITE,GATTDB_PERM_WRITE_REQ),
 CHAR_DESCRIPTOR_UUID16_WRITABLE(0x23,GATT_UUID_CHAR_CLIENT_CONFIG,GATTDB_PERM_READABLE|GATTDB_PERM_WRITE_REQ),
 CHARACTERISTIC_UUID128(0x24,0x25,CAP_UUID,GATTDB_CHAR_PROP_READ,GATTDB_PERM_READABLE),
 CHARACTERISTIC_UUID128_WRITABLE(0x26,0x27,APP_UUID,GATTDB_CHAR_PROP_READ|GATTDB_CHAR_PROP_WRITE,GATTDB_PERM_READABLE|GATTDB_PERM_WRITE_REQ),
 CHARACTERISTIC_UUID128(0x28,0x29,LINK_UUID,GATTDB_CHAR_PROP_READ,GATTDB_PERM_READABLE),

};
enum {IDLE,WAITING,SCANNING,CONNECTING,SERVICES,CHARACTERISTICS,WRITING,READING,RECONNECTING,PASS,FAILED,LINKED,PARAMETERS,MTU_WAIT};
static uint8_t state,ready,reason,ack[16],address[6],remote[6];
static uint16_t connection,service_start,service_end,value_handle,connections,disconnects,received,confirmed,invalid;
static uint32_t session,ticks,started,changed,sequence=1;
static wiced_timer_t timer;
static uint8_t command[16],command_n;
/* Counts in50ms timer callbacks; quantized scheduler time, not a wall clock. */
static uint32_t stats[4][7],queue_at[2],inbound_at,op_at;
static uint8_t stat_page;
static void measure(unsigned page,unsigned at,uint32_t age){++stats[page][at];stats[page][at+1]+=age;if(age>stats[page][at+2])stats[page][at+2]=age;}
static void timing_report(void){
 uint8_t b[40]={'S','P','T','1',1,0,0,50};b[5]=role;b[6]=stat_page;b[7]=period_ms;b[36]=selected_mode;b[37]=3;
 stats[3][0]=(selected_mode&1)?64:23;stats[3][1]=mtu;stats[3][2]=packet_bytes();stats[3][3]=period_ms;stats[3][4]=mtu_seen;
 for(unsigned i=0;i<7;i++)sp1_peer_put32(b+8+4*i,stats[stat_page][i]);
 uint16_t crc=sp1_peer_crc(b,38);b[38]=crc;b[39]=crc>>8;
 if(wiced_transport_send_data(0x7e10,b,40)==WICED_BT_SUCCESS)stat_page=(stat_page+1)%4;
}
static int relay_peek(uint8_t *out){
 if(packet_bytes()==20)return sp1_link_bridge_peek(&relay,out);
 if(!relay.count)return 0;memcpy(out,relay.queue[relay.head],32);return 1;
}
static void relay_confirm(void){
 if(!relay.count)return;
 if(packet_bytes()==32)relay.index=1;
 if(relay.index==1)measure(1,0,ticks-queue_at[relay.head]);sp1_link_bridge_confirm(&relay);
}
static int relay_receive(const uint8_t *p,unsigned n){unsigned ready_before=relay.ble.ready;int rc;
 if(packet_bytes()==20)rc=sp1_link_bridge_ble(&relay,p,n);
 else if(relay.ble.ready){++relay.ble.busy;rc=-1;}
 else if(n!=32||!p||!sp1_link_valid_frame(p)){++relay.ble.rejected;rc=0;}
 else{memcpy(relay.ble.bytes,p,32);relay.ble.ready=1;rc=1;}if(!ready_before && relay.ble.ready)inbound_at=ticks;return rc;}
static void scan_result(wiced_bt_ble_scan_results_t*,uint8_t*);
static void report(void){
 uint8_t b[40]={ 'S','P','R','1',1,role,state,reason };
 sp1_peer_put32(b+8,session);sp1_peer_put32(b+12,auto_on?election.state:sequence);
 b[16]=(uint8_t)connections;b[17]=(uint8_t)disconnects;b[18]=(uint8_t)received;b[19]=(uint8_t)confirmed;
 b[20]=(uint8_t)invalid;b[21]=ready;memcpy(b+22,address,6);memcpy(b+28,auto_on?election.peer:remote,6);
 sp1_peer_put32(b+34,ticks);uint16_t crc=sp1_peer_crc(b,38);b[38]=(uint8_t)crc;b[39]=(uint8_t)(crc>>8);
 wiced_transport_send_data(0x7e10,b,sizeof b);
}
static void transition(uint8_t s){state=s;changed=ticks;if(s!=LINKED)report();}
static void fail(uint8_t r){reason=r;transition(FAILED);wiced_bt_ble_scan(BTM_BLE_SCAN_TYPE_NONE,WICED_TRUE,scan_result);wiced_bt_start_advertisements(BTM_BLE_ADVERT_OFF,BLE_ADDR_PUBLIC,NULL);if(connection)wiced_bt_gatt_disconnect(connection);}
static int ok(wiced_bt_gatt_status_t s){return s==WICED_BT_GATT_SUCCESS||s==WICED_BT_GATT_PENDING;}
static void advertise(void){
 if(auto_on){
  uint8_t flags=6;
  wiced_bt_ble_advert_elem_t ad[]={
   {.advert_type=BTM_BLE_ADVERT_TYPE_FLAG,.len=1,.p_data=&flags},
   {.advert_type=BTM_BLE_ADVERT_TYPE_128SRV_COMPLETE,.len=16,.p_data=companion_uuid}};
  wiced_bt_ble_advert_elem_t name={.advert_type=BTM_BLE_ADVERT_TYPE_NAME_COMPLETE,.len=sizeof(SP1_BT_NAME)-1,.p_data=(uint8_t*)SP1_BT_NAME};
  if(wiced_bt_ble_set_raw_advertisement_data(2,ad)!=WICED_BT_SUCCESS||wiced_bt_ble_set_raw_scan_response_data(1,&name)!=WICED_BT_SUCCESS){fail(2);return;}
  if(!auto_advert_on){if(wiced_bt_start_advertisements(BTM_BLE_ADVERT_UNDIRECTED_HIGH,BLE_ADDR_PUBLIC,NULL)!=WICED_BT_SUCCESS)fail(2);else auto_advert_on=1;}
  return;
 }

 uint8_t flags=BTM_BLE_GENERAL_DISCOVERABLE_FLAG|BTM_BLE_BREDR_NOT_SUPPORTED;
 wiced_bt_ble_advert_elem_t ad[]={
 {.advert_type=BTM_BLE_ADVERT_TYPE_FLAG,.len=1,.p_data=&flags},
 {.advert_type=BTM_BLE_ADVERT_TYPE_128SRV_COMPLETE,.len=16,.p_data=role==3?companion_uuid:service_uuid}
 };
 transition(WAITING);
 wiced_bt_ble_advert_elem_t name={.advert_type=BTM_BLE_ADVERT_TYPE_NAME_COMPLETE,.len=sizeof(SP1_BT_NAME)-1,.p_data=(uint8_t*)SP1_BT_NAME};
 if(wiced_bt_ble_set_raw_scan_response_data(1,&name)!=WICED_BT_SUCCESS || wiced_bt_ble_set_raw_advertisement_data(2,ad)!=WICED_BT_SUCCESS || wiced_bt_start_advertisements(BTM_BLE_ADVERT_UNDIRECTED_HIGH,BLE_ADDR_PUBLIC,NULL)!=WICED_BT_SUCCESS)fail(2);
}
static void scan(void){
 fail(31);
}

static void pump(void){
 if(!connection||state!=LINKED)return;
 if(role==3){
  uint8_t fragment[32];
  if(subscribed&&relay_peek(fragment)&&wiced_bt_gatt_send_notification(connection,0x22,20,fragment)==WICED_BT_GATT_SUCCESS){relay_confirm();++confirmed;}
  return;
 }
 if(role!=2)return;
 uint8_t fragment[32];
 if(poll_write&&relay_peek(fragment)){
  union {uint32_t align;uint8_t bytes[sizeof(wiced_bt_gatt_value_t)+32];} buffer;
  memset(&buffer,0,sizeof buffer);wiced_bt_gatt_value_t *v=(void*)buffer.bytes;
  v->handle=value_handle;v->len=packet_bytes();memcpy(v->value,fragment,v->len);
  state=WRITING;changed=ticks;op_at=ticks;
  if(!ok(wiced_bt_gatt_send_write(connection,GATT_WRITE,v)))state=LINKED;
 }else{
  if(relay.ble.ready)return; /* Drain UART before consuming another remote fragment. */
  wiced_bt_gatt_read_param_t p;memset(&p,0,sizeof p);p.by_handle.handle=value_handle;
  state=READING;changed=ticks;op_at=ticks;
  if(!ok(wiced_bt_gatt_send_read(connection,GATT_READ_BY_HANDLE,&p)))state=LINKED;
 }
 poll_write=!poll_write;
}
/* SDK callbacks and periodic service share a serialized application context.
 * Guard reentrancy defensively. Never loop on busy or recurse through completions. */
static void drain_uart(void){
 if(!sp1_forward_ready(role+1))return;
 const uint8_t *pending=sp1_link_bridge_uart_peek(&relay);
 if(pending){if(wiced_transport_send_data(0x7e10,(uint8_t*)pending,32)==WICED_BT_SUCCESS){measure(1,3,ticks-inbound_at);sp1_link_bridge_uart_confirm(&relay);}else ++stats[1][6];}
}
static void service_now(int advance){
 if(service_busy||!role||!connection||state==FAILED)return;
 service_busy=1;drain_uart();if(advance)pump();service_busy=0;
}
/* Query actual link settings. A successful update request is NOT acceptance. */
static uint16_t wanted_interval(void){return role==3?0:6;}
static int read_parameters(void){
 wiced_bt_ble_conn_params_t p={0};uint16_t want=wanted_interval();stats[2][0]=want;
 if(wiced_bt_ble_get_connection_parameters(remote,&p)!=WICED_BT_SUCCESS)return 0;
 if(p.conn_interval<6||p.conn_interval>3200)return 0;
 stats[2][1]=p.conn_interval;stats[2][2]=p.conn_latency;stats[2][3]=p.supervision_timeout;++stats[2][4];
 if(want&&(p.conn_interval!=want||p.conn_latency)){++stats[2][6];return 0;}return 1;
}
static void mtu_done(uint16_t actual){
 if(actual<23||actual>517||(mtu_seen&&actual!=mtu)){fail(24);return;}mtu=actual;mtu_seen=1;
}
static void parameters_poll(void){
 if(state!=PARAMETERS||role==3)return;
 if(wanted_interval()&&role==2&&!update_sent){
  update_sent=1;stats[2][5]=wiced_bt_l2cap_update_ble_conn_params(remote,wanted_interval(),wanted_interval(),0,700)?1:2;
  if(stats[2][5]!=1){fail(21);return;}
 }
 if(read_parameters()&&(!(selected_mode&1)||mtu_seen)){transition(LINKED);service_now(1);}
 else if((ticks-changed)*period_ms>=10000)fail(22);
}
static void link_ready(void){
 if(role==3){transition(PARAMETERS);return;}
 if(role==2&&(selected_mode&1)&&!mtu_seen){
  transition(MTU_WAIT);
  if(!ok(wiced_bt_gatt_configure_mtu(connection,64)))fail(25);
 }else{transition(PARAMETERS);parameters_poll();}
}
static void discover(int chars){
 wiced_bt_gatt_discovery_param_t p;memset(&p,0,sizeof p);
 p.s_handle=chars?service_start:1;p.e_handle=chars?service_end:0xffff;
 if(!chars){p.uuid.len=16;memcpy(p.uuid.uu.uuid128,service_uuid,16);}
 transition(chars?CHARACTERISTICS:SERVICES);
 if(!ok(wiced_bt_gatt_send_discover(connection,chars?GATT_DISCOVER_CHARACTERISTICS:GATT_DISCOVER_SERVICES_BY_UUID,&p)))fail(5);
}
static void scan_result(wiced_bt_ble_scan_results_t *r,uint8_t *ad){
 (void)r;(void)ad;
}
/* APP1: one request, one retained response. Reads cannot consume/retry writes.
 * Browser uses <=20-byte request fragments and ATT long-read for the reply. */
static uint8_t app_request[24],app_response[512],app_request_n;
static uint16_t app_response_n;
void sp1_app_response(uint8_t*p,unsigned n){
 if(n>=14&&n<=sizeof(app_response)&&!memcmp(p,"SPR1",4)&&sp1_peer_crc(p,n-2)==((unsigned)p[n-2]|((unsigned)p[n-1]<<8))){memcpy(app_response,p,n);app_response_n=n;}
}
static wiced_bt_gatt_status_t app_attribute(wiced_bt_gatt_attribute_request_t *r){
 if(r->request_type==GATTS_REQ_TYPE_READ){wiced_bt_gatt_read_t*q=&r->data.read_req;
  if(q->offset>app_response_n)return WICED_BT_GATT_INVALID_OFFSET;
  unsigned n=app_response_n-q->offset;if(n>*q->p_val_len)n=*q->p_val_len;
  memcpy(q->p_val,app_response+q->offset,n);*q->p_val_len=n;return WICED_BT_GATT_SUCCESS;}
 if(r->request_type==GATTS_REQ_TYPE_WRITE){wiced_bt_gatt_write_t*q=&r->data.write_req;
  if(!q->is_prep&&!q->offset&&q->val_len>=7&&q->val_len<=244&&!memcmp(q->p_val,"SPB1",4)){
   return wiced_transport_send_data(0x7e31,q->p_val+4,q->val_len-4)==WICED_BT_SUCCESS?WICED_BT_GATT_SUCCESS:WICED_BT_GATT_INSUF_RESOURCE;}
  if(q->is_prep||q->offset||!q->val_len||q->val_len>20)return WICED_BT_GATT_INVALID_ATTR_LEN;
  if(q->val_len>=4&&!memcmp(q->p_val,"SPQ1",4))app_request_n=0;
  if(app_request_n+q->val_len>24){app_request_n=0;return WICED_BT_GATT_INVALID_ATTR_LEN;}
  memcpy(app_request+app_request_n,q->p_val,q->val_len);app_request_n+=q->val_len;
  if(app_request_n==24){app_request_n=0;
   if(memcmp(app_request,"SPQ1",4)||sp1_peer_crc(app_request,22)!=((unsigned)app_request[22]|((unsigned)app_request[23]<<8)))return WICED_BT_GATT_INVALID_ATTR_LEN;
   app_response_n=0;
   if(wiced_transport_send_data(0x7e30,app_request,24)!=WICED_BT_SUCCESS)return WICED_BT_GATT_INSUF_RESOURCE;
  }return WICED_BT_GATT_SUCCESS;
 }return WICED_BT_GATT_REQ_NOT_SUPPORTED;
}
static wiced_bt_gatt_status_t attribute(wiced_bt_gatt_attribute_request_t *r){
 if(r->conn_id!=connection)return WICED_BT_GATT_INVALID_HANDLE;
 if(role==3){
  unsigned ah=r->request_type==GATTS_REQ_TYPE_READ?r->data.read_req.handle:r->request_type==GATTS_REQ_TYPE_WRITE?r->data.write_req.handle:0;
  if(ah==0x27)return app_attribute(r);
  if(ah==0x29&&r->request_type==GATTS_REQ_TYPE_READ){wiced_bt_gatt_read_t*q=&r->data.read_req;uint8_t b[2]={mtu-3,0};if(q->offset||*q->p_val_len<2)return WICED_BT_GATT_INVALID_ATTR_LEN;memcpy(q->p_val,b,2);*q->p_val_len=2;return WICED_BT_GATT_SUCCESS;}
  if(r->request_type==GATTS_REQ_TYPE_MTU){mtu=r->data.mtu;return WICED_BT_GATT_SUCCESS;}
  if(r->request_type==GATTS_REQ_TYPE_READ){
   wiced_bt_gatt_read_t *q=&r->data.read_req;
   const uint8_t caps[12]={'S','P','C','G',1,4,56,20,1,0,0,0};
   uint8_t ccc[2]={subscribed,0};const uint8_t *v=q->handle==0x25?caps:ccc;
   unsigned n=q->handle==0x25?sizeof caps:2;
   if(q->handle!=0x25&&q->handle!=0x23)return WICED_BT_GATT_INVALID_HANDLE;
   if(q->offset)return WICED_BT_GATT_INVALID_OFFSET;
   if(*q->p_val_len<n)return WICED_BT_GATT_INVALID_ATTR_LEN;
   memcpy(q->p_val,v,n);*q->p_val_len=n;return WICED_BT_GATT_SUCCESS;
  }
  if(r->request_type==GATTS_REQ_TYPE_WRITE){
   wiced_bt_gatt_write_t *q=&r->data.write_req;
   if(q->is_prep||q->offset)return WICED_BT_GATT_WRITE_NOT_PERMIT;
   if(q->handle==0x23){
    if(q->val_len!=2||q->p_val[1]||q->p_val[0]>1)return WICED_BT_GATT_INVALID_ATTR_LEN;
    subscribed=q->p_val[0];if(subscribed){transition(LINKED);}else wiced_bt_gatt_disconnect(connection);return WICED_BT_GATT_SUCCESS;
   }
   if(q->handle!=0x22||!subscribed)return WICED_BT_GATT_WRITE_NOT_PERMIT;
   unsigned before=relay.ble.rejected;int result=relay_receive(q->p_val,q->val_len);
   if(result<0)return WICED_BT_GATT_INSUF_RESOURCE;
   if(relay.ble.rejected!=before)return WICED_BT_GATT_INVALID_ATTR_LEN;
   /* Only transport ACK / HELLO / READY may reach the controller. Never DATA. */
   if(relay.ble.ready&&(relay.ble.bytes[3]<2||relay.ble.bytes[3]>4)){
    sp1_link_bridge_uart_confirm(&relay);return WICED_BT_GATT_WRITE_NOT_PERMIT;
   }
   ++received;service_now(0);return WICED_BT_GATT_SUCCESS;
  }
  return WICED_BT_GATT_REQ_NOT_SUPPORTED;
 }
 if(r->request_type==GATTS_REQ_TYPE_MTU){mtu_done(r->data.mtu);return WICED_BT_GATT_SUCCESS;}
 if(r->request_type==GATTS_REQ_TYPE_READ){
  wiced_bt_gatt_read_t *q=&r->data.read_req;
  if(q->handle!=0x12||role!=1)return WICED_BT_GATT_INVALID_HANDLE;
  if(q->offset)return WICED_BT_GATT_INVALID_OFFSET;
  if(*q->p_val_len<packet_bytes())return WICED_BT_GATT_INVALID_ATTR_LEN;
  uint8_t fragment[32]={0};int available=relay_peek(fragment);
  memcpy(q->p_val,fragment,packet_bytes());*q->p_val_len=packet_bytes();
  /* GATT read is only a relay dequeue; end-controller ACK handles lost responses. */
  if(available){relay_confirm();++confirmed;}return WICED_BT_GATT_SUCCESS;
 }
 if(r->request_type==GATTS_REQ_TYPE_WRITE){
  wiced_bt_gatt_write_t *q=&r->data.write_req;
  if(role!=1||q->handle!=0x12||q->is_prep||q->offset||state!=LINKED)return WICED_BT_GATT_WRITE_NOT_PERMIT;
  unsigned invalid_before=relay.ble.rejected;
  int result=relay_receive(q->p_val,q->val_len);
  if(result<0)return WICED_BT_GATT_INSUF_RESOURCE;
  if(relay.ble.rejected!=invalid_before){++invalid;return WICED_BT_GATT_INVALID_ATTR_LEN;}
  ++received;service_now(0);return WICED_BT_GATT_SUCCESS;
 }
 return WICED_BT_GATT_REQ_NOT_SUPPORTED;
}
static wiced_bt_gatt_status_t gatt(wiced_bt_gatt_evt_t event,wiced_bt_gatt_event_data_t *e){
 if(event==GATT_ATTRIBUTE_REQUEST_EVT){
  wiced_bt_gatt_attribute_request_t *q=&e->attribute_request;
  if(q->request_type==GATTS_REQ_TYPE_READ&&q->data.read_req.handle==3){
   static const uint8_t name[]=SP1_BT_NAME;wiced_bt_gatt_read_t *r=&q->data.read_req;
   if(r->offset>sizeof(name)-1)return WICED_BT_GATT_INVALID_OFFSET;
   unsigned n=sizeof(name)-1-r->offset;if(n>*r->p_val_len)n=*r->p_val_len;
   memcpy(r->p_val,name+r->offset,n);*r->p_val_len=n;return WICED_BT_GATT_SUCCESS;
  }
  if(auto_on){
   unsigned handle=q->request_type==GATTS_REQ_TYPE_READ?q->data.read_req.handle:q->request_type==GATTS_REQ_TYPE_WRITE?q->data.write_req.handle:0;
   unsigned wanted=sr_handle_mode(handle);
   if(handle&&!wanted)return WICED_BT_GATT_INVALID_HANDLE;
   if(wanted){
    if(!sp1_claim(wanted))return WICED_BT_GATT_WRITE_NOT_PERMIT;
    role=wanted==5?4:wanted==4?3:1;
    if(wanted==2&&state==WAITING)link_ready();
   }
   if(sp1_active_mode()==6&&q->request_type==GATTS_REQ_TYPE_MTU){mtu_done(q->data.mtu);return WICED_BT_GATT_SUCCESS;}
  }
  return attribute(q);
 }
 if(event==GATT_CONNECTION_STATUS_EVT){
  wiced_bt_gatt_connection_status_t *c=&e->connection_status;
  if(c->connected){
   uint8_t expected[6]={0xc2,0x53,0x50,0x31,0x46,role==1?2:1};
   if((!auto_on&&!role)||state==FAILED||state==PASS||connection||(!auto_on&&role!=3&&(c->addr_type!=BLE_ADDR_PUBLIC||memcmp(c->bd_addr,expected,6)))||(auto_on&&(sp1_active_mode()==1||sp1_election_busy()))){wiced_bt_gatt_disconnect(c->conn_id);return WICED_BT_GATT_SUCCESS;}
   app_request_n=0;app_response_n=0;subscribed=0;connection=c->conn_id;memcpy(remote,c->bd_addr,6);++connections;sp1_link_bridge_reset(&relay);poll_write=0;update_sent=0;stats[2][5]=0;mtu=23;mtu_seen=0;
   service_start=service_end=value_handle=0;
   if(auto_on){sp1_connection(1,0);wiced_bt_ble_scan(BTM_BLE_SCAN_TYPE_NONE,WICED_TRUE,scan_result);}
   if(role==2)discover(0);else if(auto_on&&sp1_active_mode()==6)transition(WAITING);else link_ready();
  }else if(c->conn_id==connection){
   app_request_n=0;app_response_n=0;subscribed=0;connection=0;++disconnects;
   if(auto_on){if(sp1_active_mode()!=1)sp1_connection(0,0);}
   if(state==FAILED||state==PASS)return WICED_BT_GATT_SUCCESS;
   sp1_link_bridge_reset(&relay);transition(RECONNECTING);

  }
 }else if(event==GATT_DISCOVERY_RESULT_EVT&&e->discovery_result.conn_id==connection){
  if(state==SERVICES&&GATT_DISCOVERY_RESULT_SERVICE_UUID_LEN(e)==16&&!memcmp(GATT_DISCOVERY_RESULT_SERVICE_UUID128(e),service_uuid,16)){
   if(service_start){fail(8);return WICED_BT_GATT_SUCCESS;}
   service_start=GATT_DISCOVERY_RESULT_SERVICE_START_HANDLE(e);service_end=GATT_DISCOVERY_RESULT_SERVICE_END_HANDLE(e);
  }else if(state==CHARACTERISTICS&&GATT_DISCOVERY_RESULT_CHARACTERISTIC_UUID_LEN(e)==16&&!memcmp(GATT_DISCOVERY_RESULT_CHARACTERISTIC_UUID128(e),value_uuid,16)){
   if(value_handle){fail(9);return WICED_BT_GATT_SUCCESS;}
   value_handle=GATT_DISCOVERY_RESULT_CHARACTERISTIC_VALUE_HANDLE(e);
  }
 }else if(event==GATT_DISCOVERY_CPLT_EVT&&e->discovery_complete.conn_id==connection){
  if(e->discovery_complete.status!=WICED_BT_GATT_SUCCESS){fail(10);return WICED_BT_GATT_SUCCESS;}
  if(state==SERVICES){if(service_start&&service_end>=service_start)discover(1);else fail(11);}
  else if(state==CHARACTERISTICS){if(value_handle>=service_start&&value_handle<=service_end){link_ready();}else fail(12);}
 }else if(event==GATT_OPERATION_CPLT_EVT&&e->operation_complete.conn_id==connection&&role==2){
  wiced_bt_gatt_operation_complete_t *c=&e->operation_complete;
  if(state==FAILED||state==PASS)return WICED_BT_GATT_SUCCESS;
  if(state==MTU_WAIT){
   if(c->op!=GATTC_OPTYPE_CONFIG||c->status!=WICED_BT_GATT_SUCCESS){fail(26);return WICED_BT_GATT_SUCCESS;}
   mtu_done(c->response_data.mtu);if(state!=FAILED){transition(PARAMETERS);parameters_poll();}return WICED_BT_GATT_SUCCESS;
  }
  if(state!=WRITING&&state!=READING)return WICED_BT_GATT_SUCCESS;
  measure(0,state==READING?0:3,ticks-op_at);
  if(c->status!=WICED_BT_GATT_SUCCESS){++invalid;transition(LINKED);return WICED_BT_GATT_SUCCESS;}
  if(state==WRITING&&c->op==GATTC_OPTYPE_WRITE){relay_confirm();++confirmed;transition(LINKED);service_now(1);}
  else if(state==READING&&c->op==GATTC_OPTYPE_READ){
   wiced_bt_gatt_data_t *v=&c->response_data.att_value;
   if(v->handle!=value_handle||v->offset||v->len!=packet_bytes()){++invalid;fail(15);return WICED_BT_GATT_SUCCESS;}
   if(v->p_data[0]){unsigned before=relay.ble.rejected;int result=relay_receive(v->p_data,v->len);if(result>=0&&relay.ble.rejected==before)++received;else ++invalid;}
   if(!v->p_data[0])++stats[0][6];
   transition(LINKED);service_now(v->p_data[0]!=0 || relay.count!=0);
  }

 }
 return WICED_BT_GATT_SUCCESS;
}
static void start_role(uint8_t r,uint32_t token,uint8_t mode,uint8_t which){
 if(!ready||role||r<1||r>3||!token)return;
 (void)which;(void)mode;
 role=r;fast_mode=1;period_ms=5;wiced_stop_timer(&timer);
 if(wiced_start_timer(&timer,period_ms)!=WICED_BT_SUCCESS){fail(27);return;}report_at=timing_at=0;session=token;started=ticks;
 wiced_bt_dev_read_local_addr(address);
 if(!rb_address(address)){fail(18);return;}

 if(role==1||role==3)advertise();else scan();
}
static void tick(uint32_t unused){
 static uint8_t discovery_expired;
 (void)unused;++ticks;

 if(auto_on&&!discovery_expired&&sp1_active_mode()==6&&(ticks-discovery_at)*period_ms>120000){
  discovery_expired=1;wiced_bt_dev_set_discoverability(BTM_NON_DISCOVERABLE,0,0);wiced_bt_dev_set_connectability(BTM_NON_CONNECTABLE,0,0);
  wiced_bt_ble_scan(BTM_BLE_SCAN_TYPE_NONE,WICED_TRUE,scan_result);
  wiced_bt_start_advertisements(BTM_BLE_ADVERT_OFF,BLE_ADDR_PUBLIC,NULL);
 }
 if(role&&state!=FAILED&&(!auto_on||sp1_active_mode()!=6)){
  if(state==RECONNECTING&&!connection&&(ticks-changed)*period_ms>=2000){
   if(auto_on){if(role==2){transition(CONNECTING);if(!wiced_bt_gatt_le_connect(remote,BLE_ADDR_PUBLIC,BLE_CONN_MODE_HIGH_DUTY,WICED_TRUE))fail(6);}else advertise();}
   else if(role==1||role==3)advertise();else scan();
  }
  if((state==WRITING||state==READING||state==SERVICES||state==CHARACTERISTICS||state==CONNECTING||state==MTU_WAIT)&&(ticks-changed)*period_ms>=20000){fail(20);return;}
  if(role<=3)parameters_poll();
  if(state==LINKED||state==READING||state==WRITING){drain_uart();pump();}
 }
 if((ticks-report_at)*period_ms>=1000){report_at=ticks;report();}
 if(connection && role<=3 && (!auto_on||sp1_active_mode()!=6) && (ticks-timing_at)*period_ms>=1000){timing_at=ticks;
  if(state==LINKED||state==READING||state==WRITING){if(role!=3&&!read_parameters()){fail(23);return;}}
  timing_report();
 }
}
static wiced_result_t management(wiced_bt_management_evt_t event,wiced_bt_management_evt_data_t *d){
 if(event==BTM_ENABLED_EVT){
  if(d->enabled.status!=WICED_BT_SUCCESS)return WICED_BT_ERROR;
  if(wiced_bt_gatt_register(gatt)!=WICED_BT_GATT_SUCCESS||wiced_bt_gatt_db_init(database,sizeof database)!=WICED_BT_GATT_SUCCESS)return WICED_BT_ERROR;
  if(wiced_init_timer(&timer,tick,0,WICED_MILLI_SECONDS_PERIODIC_TIMER)!=WICED_BT_SUCCESS||wiced_start_timer(&timer,50)!=WICED_BT_SUCCESS)return WICED_BT_ERROR;
  ready=1;wiced_bt_dev_read_local_addr(address);report();return WICED_BT_SUCCESS;
 }
 return WICED_BT_ERROR;
}

wiced_result_t sp1_peer_start(unsigned mode,uint32_t nonce){
 if(mode!=3)return WICED_BT_ERROR;
 wiced_bt_management_evt_data_t d;memset(&d,0,sizeof d);d.enabled.status=WICED_BT_SUCCESS;
 wiced_result_t rc=management(BTM_ENABLED_EVT,&d);
 if(rc!=WICED_BT_SUCCESS)return rc;
 start_role(mode,nonce,5,0);return role==mode&&state!=FAILED?WICED_BT_SUCCESS:WICED_BT_ERROR;
}
void sp1_peer_input(uint8_t *p,uint32_t n){
 /* Role is fixed by the common mode selector for this radio boot. Only
  * framed bridge data reaches here; do not parse embedded role commands. */
 for(uint32_t i=0;i<n;i++)if(connection){unsigned before=relay.count;sp1_link_bridge_uart(&relay,p[i]);if(relay.count>before)queue_at[(relay.head+relay.count-1)%2]=ticks;}
 if(relay.count)service_now(1);
}

wiced_result_t sp1_auto_start(uint32_t token,const uint8_t *id){
 auto_on=1;session=token;period_ms=5;
 (void)id;wiced_bt_dev_read_local_addr(address);
 if(!rb_address(address))return WICED_BT_ERROR;
 wiced_bt_management_evt_data_t d;memset(&d,0,sizeof d);d.enabled.status=WICED_BT_SUCCESS;
 wiced_result_t rc=management(BTM_ENABLED_EVT,&d);if(rc!=WICED_BT_SUCCESS)return rc;
 wiced_stop_timer(&timer);if(wiced_start_timer(&timer,period_ms)!=WICED_BT_SUCCESS)return WICED_BT_ERROR;
 discovery_at=ticks;transition(WAITING);advertise();
 /* Negotiation consumes changing advertisements from the SAME peer. */
 return state!=FAILED?WICED_BT_SUCCESS:WICED_BT_ERROR;
}
int sp1_relay_discover(void){
 return 0;
}
void sp1_auto_discovery_stop(unsigned winner){
 if(!auto_on)return;
 if(winner==1&&election.state==EL_READY)return; /* Repeat READY before stopping BLE; reserve audio peer. */
 wiced_bt_ble_scan(BTM_BLE_SCAN_TYPE_NONE,WICED_TRUE,scan_result);
 wiced_bt_start_advertisements(BTM_BLE_ADVERT_OFF,BLE_ADDR_PUBLIC,NULL);
 auto_advert_on=0;
 if(winner==1){if(connection)wiced_bt_gatt_disconnect(connection);}
 else{wiced_bt_dev_set_discoverability(BTM_NON_DISCOVERABLE,BTM_DEFAULT_DISC_WINDOW,BTM_DEFAULT_DISC_INTERVAL);wiced_bt_dev_set_connectability(WICED_FALSE,BTM_DEFAULT_CONN_WINDOW,BTM_DEFAULT_CONN_INTERVAL);}
}
