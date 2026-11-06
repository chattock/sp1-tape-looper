/* Private experimental A2DP source for SP-1. No flash operations.
 * Source APIs follow the pinned Infineon audio-watch example.
 * Unlike that board's example, SP-1 MUST be an I2S clock slave.
 * Nothing connects until the controller supplies an exact receiver address.
 */
#include <string.h>
#include "sparcommon.h"
#include "app_bt_cfg.h"
#include "wiced_bt_stack.h"
#include "wiced_bt_sdp.h"
#include "wiced_bt_dev.h"
#include "wiced_bt_ble.h"
#include "wiced_bt_avdt.h"
#include "wiced_bt_a2d.h"
#include "wiced_bt_a2d_sbc.h"
#include "wiced_bt_audio.h"
#include "wiced_bt_trace.h"
#include "wiced_transport.h"
#include "wiced_timer.h"
#include "wiced_memory.h"
#include "codec_policy.h"
#include "../video-protocol/source_protocol.h"
_Static_assert(AVDT_CODEC_SIZE>=8,"codec diagnostic bounds");
_Static_assert(AUDIO_ROUTE_I2S==0&&AUDIO_ROUTE_SINE==2,"source route protocol");

extern const wiced_bt_cfg_buf_pool_t wiced_bt_cfg_buf_pools[];
static uint32_t failed,ticks,flags,started;
static unsigned startup_pending;
extern void sp1_connection(unsigned,unsigned);
/* magic/version/phase/error/events/route/lcid/mtu/min-free/ticks */
static uint32_t diag[10]={0x31525356,4};
static unsigned sep_count,sep_cursor,last_cap_error;
static uint8_t identity[8],peer[6],own[6],handle,seid,route;
static uint16_t lcid;
static wiced_bool_t configured,routing;
static wiced_timer_t timer;
static wiced_bt_avdt_sep_info_t seps[8];
static wiced_bt_avdt_cfg_t remote_cfg,selected_cfg;
static wiced_bt_a2d_sbc_cie_t chosen={A2D_SBC_IE_SAMP_FREQ_48,
 A2D_SBC_IE_CH_MD_JOINT,A2D_SBC_IE_BLOCKS_16,A2D_SBC_IE_SUBBAND_8,
 A2D_SBC_IE_ALLOC_MD_L,53,2};
static int16_t tone[256] __attribute__((aligned(4)));
static uint32_t u32(const uint8_t*p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static unsigned u16(const uint8_t*p){return p[0]|((unsigned)p[1]<<8);}
static void report(void){
 /* v2: words8/9 retain the last raw codec IE, not heap/tick counters. */
 wiced_transport_send_data(0x7e04,(uint8_t*)diag,sizeof diag);sp1_connection(flags&1,!!(flags&2));
}
static void fail(unsigned phase,unsigned code){
 if(failed)return; /* Retain first failure. */
 diag[2]=phase;diag[3]=code?code:0xffff;failed=1;startup_pending=0;
 wiced_bt_set_pairable_mode(0,0);
 if(routing){wiced_audio_stop(lcid);routing=WICED_FALSE;}flags=0;report();
}
static void event(uint8_t h,wiced_bt_device_address_t addr,uint8_t e,wiced_bt_avdt_ctrl_t*d);
static void capabilities(uint8_t,wiced_bt_device_address_t,uint8_t,wiced_bt_avdt_ctrl_t*);
static void next_endpoint(void){
 while(sep_cursor<sep_count){
  wiced_bt_avdt_sep_info_t *s=&seps[sep_cursor++];
  if(s->in_use||s->tsep!=AVDT_TSEP_SNK||s->media_type!=AVDT_MEDIA_AUDIO)continue;
  seid=s->seid;diag[2]=4;diag[5]=route|((uint32_t)seid<<8)|((uint32_t)sep_cursor<<16)|((uint32_t)sep_count<<24);
  memset(&remote_cfg,0,sizeof remote_cfg);
  unsigned rc=wiced_bt_avdt_get_cap_req(peer,seid,&remote_cfg,capabilities);
  if(rc)fail(4,0x10000u|rc);
  return;
 }
 fail(4,last_cap_error?last_cap_error:0x70000u);
}
static void capabilities(uint8_t h,wiced_bt_device_address_t addr,uint8_t e,wiced_bt_avdt_ctrl_t*d){
 (void)h;if(failed)return;++diag[4];
 if(!addr||memcmp(addr,peer,6)){fail(4,0x20000u|e);return;}
 if(e!=AVDT_GETCAP_CFM_EVT||diag[2]!=4){fail(4,0x30000u|e);return;}
 if(!d){fail(4,0x40000u|e);return;}
 if(d->getcap_cfm.hdr.err_code){last_cap_error=0x50000u|d->getcap_cfm.hdr.err_code;next_endpoint();return;}
 if(!d->getcap_cfm.p_cfg){last_cap_error=0x60001u;next_endpoint();return;}
 wiced_bt_avdt_cfg_t *cfg=d->getcap_cfm.p_cfg;
 uint8_t *raw=cfg->codec_info;uint8_t selected[7];
 /* Fixed bounds: SDK allocates AVDT_CODEC_SIZE bytes, never trust remote LOSC. */
 /* v4 words8/9 retain the selected remote name prefix. */
 unsigned policy=sp1_sbc_select(raw,sizeof cfg->codec_info,selected);
 if(cfg->num_codec!=1||policy){last_cap_error=0x61000u|((unsigned)raw[2]<<4)|policy;next_endpoint();return;}
 unsigned parsed=wiced_bt_a2d_pars_sbc_info(&chosen,selected,WICED_FALSE);
 if(parsed!=A2D_SUCCESS){last_cap_error=0x62000u|parsed;next_endpoint();return;}
 memset(&selected_cfg,0,sizeof selected_cfg);selected_cfg.num_codec=1;selected_cfg.psc_mask=AVDT_PSC_TRANS;
 unsigned rc=wiced_bt_a2d_bld_sbc_info(AVDT_MEDIA_AUDIO,&chosen,selected_cfg.codec_info);
 if(rc){fail(4,0x63000u|rc);return;}
 diag[2]=5;rc=wiced_bt_avdt_open_req(handle,peer,seid,&selected_cfg);if(rc)fail(5,rc);
}
static void discovered(uint8_t h,wiced_bt_device_address_t addr,uint8_t e,wiced_bt_avdt_ctrl_t*d){
 (void)h;if(failed)return;++diag[4];
 if(!addr||memcmp(addr,peer,6)){fail(3,0x20000u|e);return;}
 if(e!=AVDT_DISCOVER_CFM_EVT||diag[2]!=3){fail(3,0x30000u|e);return;}
 if(!d){fail(3,0x40000u|e);return;}
 if(d->discover_cfm.hdr.err_code){fail(3,0x50000u|d->discover_cfm.hdr.err_code);return;}
 if(d->discover_cfm.num_seps>8||!d->discover_cfm.p_sep_info){fail(3,2);return;}
 sep_count=d->discover_cfm.num_seps;sep_cursor=0;last_cap_error=0;
 memmove(seps,d->discover_cfm.p_sep_info,sep_count*sizeof seps[0]);
 next_endpoint();
}
/* Match the pinned audio-watch initiator sequence: connect, then discover.
 * Phase10 distinguishes signaling setup from endpoint discovery (phase3).
 * No retry: retain the first error and require a new user-directed boot. */
static void connected(uint8_t h,wiced_bt_device_address_t addr,uint8_t e,wiced_bt_avdt_ctrl_t*d){
 (void)h;if(failed)return;++diag[4];
 if(!addr||memcmp(addr,peer,6)){fail(10,0x20000u|e);return;}
 if(e!=AVDT_CONNECT_IND_EVT){fail(10,0x30000u|e);return;}
 if(!d){fail(10,0x40000u|e);return;}
 if(d->connect_ind.err_code){fail(10,0x50000u|d->connect_ind.err_code);return;}
 if(diag[2]!=10){fail(10,0x60000u|e);return;}
 diag[2]=3;
 unsigned rc=wiced_bt_avdt_discover_req(peer,seps,8,discovered);
 if(rc)fail(3,0x10000u|rc);
}
static void event(uint8_t h,wiced_bt_device_address_t addr,uint8_t e,wiced_bt_avdt_ctrl_t*d){
 (void)h;++diag[4];if(failed)return;
 if(!configured||!addr||memcmp(addr,peer,6)){fail(9,1);return;}
 if(e==AVDT_OPEN_CFM_EVT){
  if(diag[2]!=5){fail(5,0x72000u|diag[2]);return;}
  if(!d||d->open_cfm.hdr.err_code){fail(5,d?d->open_cfm.hdr.err_code:1);return;}
  lcid=d->open_cfm.lcid;diag[6]=lcid;diag[7]=d->open_cfm.peer_mtu;flags=1;diag[2]=6;
  startup_pending=1; /* Allow OPEN diagnostic to drain before START. */
 }else if(e==AVDT_START_CFM_EVT){
  if(!d||d->start_cfm.err_code){fail(6,d?d->start_cfm.err_code:1);return;}
  if(diag[2]!=12){fail(13,0x71000u|diag[2]);return;}
  diag[2]=13;startup_pending=3; /* START accepted; route not started yet. */
 }else if(e==AVDT_DISCONNECT_IND_EVT||e==AVDT_CLOSE_IND_EVT||e==AVDT_CLOSE_CFM_EVT||e==AVDT_SUSPEND_IND_EVT){
  fail(8,e+1);
 }else if(e==AVDT_CONFIG_IND_EVT||e==AVDT_OPEN_IND_EVT||e==AVDT_START_IND_EVT){
  fail(9,e+1); /* Initiator-only bounded probe. */
 }
 report();
}
static uint8_t source_sdp[]={
 SDP_ATTR_SEQUENCE_1(79),SDP_ATTR_SEQUENCE_1(77),
 SDP_ATTR_RECORD_HANDLE(0x10001),SDP_ATTR_CLASS_ID(UUID_SERVCLASS_AUDIO_SOURCE),
 SDP_ATTR_ID(ATTR_ID_PROTOCOL_DESC_LIST),SDP_ATTR_SEQUENCE_1(16),
 SDP_ATTR_SEQUENCE_1(6),SDP_ATTR_UUID16(UUID_PROTOCOL_L2CAP),SDP_ATTR_VALUE_UINT2(BT_PSM_AVDTP),
 SDP_ATTR_SEQUENCE_1(6),SDP_ATTR_UUID16(UUID_PROTOCOL_AVDTP),SDP_ATTR_VALUE_UINT2(0x103),
 SDP_ATTR_ID(ATTR_ID_BT_PROFILE_DESC_LIST),SDP_ATTR_SEQUENCE_1(8),
 SDP_ATTR_SEQUENCE_1(6),SDP_ATTR_UUID16(UUID_SERVCLASS_ADV_AUDIO_DISTRIBUTION),SDP_ATTR_VALUE_UINT2(0x103),
 SDP_ATTR_UINT2(ATTR_ID_SUPPORTED_FEATURES,1),SDP_ATTR_SERVICE_NAME(16),
 'S','P','1',' ','A','u','d','i','o',' ','P','r','o','b','e',' '
};
static unsigned initialize_source(void){
 if(!wiced_bt_sdp_db_init(source_sdp,sizeof source_sdp))return WICED_BT_ERROR;
 wiced_bt_audio_config_buffer_t buffers={WICED_AUDIO_SOURCE_ROLE,8192,8192};
 unsigned rc=wiced_audio_buffer_initialize(buffers);if(rc)return rc;
 wiced_bt_avdt_reg_t reg={1691,4,4,10,0};
 wiced_bt_avdt_register(&reg,event);
 wiced_bt_avdt_cs_t cs;memset(&cs,0,sizeof cs);
 cs.cfg.num_codec=1;cs.cfg.psc_mask=AVDT_PSC_TRANS;cs.tsep=AVDT_TSEP_SRC;
 cs.nsc_mask=AVDT_NSC_RECONFIG;cs.p_ctrl_cback=event;cs.media_type=AVDT_MEDIA_AUDIO;
 rc=wiced_bt_a2d_bld_sbc_info(AVDT_MEDIA_AUDIO,&chosen,cs.cfg.codec_info);
 if(rc)return rc;
 return wiced_bt_avdt_create_stream(&handle,&cs);
}
/* 100 ms startup steps separate control packets from vendor route setup.
 * No retries, no master clocks, and no unverified UART register writes.
 * v3 phases: 11 START next, 12 START submitted, 13 START confirmed,
 * 14 timing configured, 15 audio_start next, 7 audio_start returned.
 * Stage15 is transmitted a full timer interval BEFORE the vendor call. */
static void tick(uint32_t unused){
 (void)unused;++ticks;
 if(failed)return;
 if(configured&&diag[2]!=7&&ticks-started>400){fail(diag[2],0xfffe);return;}
 if(startup_pending==1){startup_pending=2;diag[2]=11;report();return;}
 if(startup_pending==2){
  startup_pending=0;diag[2]=12;
  unsigned rc=wiced_bt_avdt_start_req(&handle,1);
  if(rc){fail(12,rc);return;}report();return;
 }
 if(startup_pending==3){
  startup_pending=4;
  if(route==AUDIO_ROUTE_SINE)wiced_audio_set_sinwave(tone);
  wiced_audio_use_sw_timing(route==AUDIO_ROUTE_SINE);
  diag[2]=14;report();return;
 }
 if(startup_pending==4){startup_pending=5;diag[2]=15;report();return;}
 if(startup_pending==5){
  startup_pending=0;
  wiced_audio_start(WICED_FALSE,route,lcid,&chosen);
  if(failed){wiced_audio_stop(lcid);return;}
  routing=WICED_TRUE;flags=3;diag[2]=7;report();return;
 }
 if(ticks%10==0)report();
}

wiced_result_t sp1_source_start(void){
 for(unsigned i=0;i<128;i++)for(unsigned c=0;c<2;c++){unsigned x=(i*(c+1))%128;tone[2*i+c]=(int16_t)((x<64?(int)x:128-(int)x)*48-1536);}

 wiced_bt_dev_read_local_addr(own);
 unsigned rc=initialize_source();if(rc){fail(1,rc);return WICED_BT_ERROR;}
 diag[2]=2;wiced_init_timer(&timer,tick,0,WICED_MILLI_SECONDS_PERIODIC_TIMER);wiced_start_timer(&timer,100);report();return WICED_BT_SUCCESS;
}
static void named(wiced_bt_dev_remote_name_result_t *n){
 if(failed||!configured||diag[2]!=16)return;
 if(!n||n->status||memcmp(n->bd_addr,peer,6)){fail(16,n?n->status+1:1);return;}
 memset(diag+8,0,8);unsigned length=n->length;if(length>8)length=8;
 memcpy(diag+8,n->remote_bd_name,length);
 diag[2]=10;
 unsigned rc=wiced_bt_avdt_connect_req(peer,0,connected);if(rc)fail(10,0x10000u|rc);
 report();
}
int sp1_source_connect(const uint8_t *q,unsigned n,uint32_t token){
 if(failed||!video_packet_valid(q,n,token,own,configured))return 0;
 memcpy(peer,q+10,6);route=q[8];diag[5]=route;configured=WICED_TRUE;diag[2]=16;started=ticks;
 wiced_bt_set_pairable_mode(1,0);
 unsigned rc=wiced_bt_dev_get_remote_name(peer,named);if(rc!=WICED_BT_PENDING){fail(16,0x10000u|rc);return 0;}return 1;
}
