/* Classic Bluetooth audio discovery; BLE advertising is not audio discovery.
 * Explicit request, bounded inquiry, serial SDP verification, no blind retries.
 * No NVRAM or audio buffer allocation in this module. */
#include <string.h>
#include "wiced_bt_stack.h"
#include "wiced_bt_dev.h"
#include "wiced_bt_sdp.h"
#include "wiced_timer.h"
#include "wiced_transport.h"
#include "discovery_policy.h"
extern int sp1_discovery_connect(const uint8_t*,unsigned);
static struct ad_table table;
static uint32_t db_storage[768];
static wiced_bt_sdp_discovery_db_t *const db=(void*)db_storage;
static wiced_timer_t timer;
static unsigned state,cursor,seconds,direction,error,initialized;
/* 0 idle,1 inquiry,2 SDP,3 choose,4 selected,5 failed. */
static void report(void){
 uint32_t r[10]={0x31444341,1,state,direction,table.count,cursor,error,table.overflow,table.uncertain,seconds};
 wiced_transport_send_data(0x7e07,(uint8_t*)r,sizeof r);
 for(unsigned i=0;i<table.count;i++){
  uint8_t p[16]={'A','D','R','1',(uint8_t)i,table.entries[i].compatible};memcpy(p+6,table.entries[i].address,6);
  wiced_transport_send_data(0x7e08,p,sizeof p);
 }
}
static void next(void);
static void complete(uint16_t result){
 if(state!=2)return;
 if(result&&result!=WICED_BT_SDP_NO_RECS_MATCH) table.uncertain=1;
 else if(!result) table.entries[cursor].compatible=wiced_bt_sdp_find_service_in_db(db,direction?0x110b:0x110a,NULL)!=NULL;
 ++cursor;seconds=0;next();
}
int sp1_discovery_select(unsigned n){
 if(state!=3||n>=table.count||!table.entries[n].compatible)return 0;
 cursor=n;state=4;
 if(!sp1_discovery_connect(table.entries[n].address,direction)){state=5;error=4;}
 report();return state==4;
}
static void next(void){
 if(cursor==table.count){state=3;int n=ad_unique(&table);report();if(n>=0)sp1_discovery_select((unsigned)n);return;}
 wiced_bt_uuid_t uuid={.len=2};uuid.uu.uuid16=direction?0x110b:0x110a;
 uint16_t attributes[]={0x0001};
 state=2;
 if(!wiced_bt_sdp_init_discovery_db(db,sizeof db_storage,1,&uuid,1,attributes)||
    !wiced_bt_sdp_service_search_attribute_request(table.entries[cursor].address,db,complete)){
  /* No retry and no automatic selection after incomplete capability checks. */
  state=5;table.uncertain=1;error=2;
 }
 report();
}
static void result(wiced_bt_dev_inquiry_scan_result_t *r,uint8_t *eir){
 (void)eir;if(state!=1)return;
 if(r){ad_add(&table,r->remote_bd_addr);return;}
 seconds=0;cursor=0;next();
}
static void tick(uint32_t unused){
 (void)unused;if(state!=1&&state!=2)return;
 if(++seconds<(state==1?20:12))return;
 unsigned old=state;state=5;error=3;table.uncertain=1;
 if(old==1)wiced_bt_cancel_inquiry();else wiced_bt_sdp_cancel_service_search(db);
 report();
}
int sp1_discovery_start(unsigned outgoing){
 /* One search per radio boot; prevents late callback reuse across sessions. */
 if(state||outgoing>1)return 0;
 memset(&table,0,sizeof table);direction=outgoing;state=1;seconds=0;error=0;
 if(!initialized){wiced_init_timer(&timer,tick,0,WICED_SECONDS_PERIODIC_TIMER);wiced_start_timer(&timer,1);initialized=1;}
 wiced_bt_dev_inq_parms_t p;memset(&p,0,sizeof p);p.mode=BTM_GENERAL_INQUIRY;p.duration=8;
 if(wiced_bt_start_inquiry(&p,result)!=WICED_BT_PENDING){state=5;error=1;}
 report();return state==1;
}
void sp1_discovery_status(void){report();}
