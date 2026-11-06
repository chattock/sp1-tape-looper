/* Local A2DP compressed-route BUILD PROBE. Not approved for device install.
 * No I2S pin setup, recording, NVRAM or flash programming. */
#include <string.h>
#include "auto.h"
#include "sparcommon.h"
#include "app_bt_cfg.h"
#include "wiced_bt_stack.h"
#include "wiced_bt_dev.h"
#include "wiced_bt_sdp.h"
#include "wiced_bt_a2dp_sink.h"
#include "wiced_bt_audio.h"
#include "wiced_transport.h"
#include "wiced_bt_trace.h"
#include "wiced_timer.h"
#include "wiced_bt_avdt.h"
#include "wiced_memory.h"
extern const wiced_bt_cfg_buf_pool_t wiced_bt_cfg_buf_pools[];
/* Legacy startup probe counters are retired; common state and relay media
 * counters carry current diagnostics without a second periodic report. */
void sp1_trace_route_result(wiced_result_t result){(void)result;}
/* BTID199: documented legacy AVDTP BT_HDR callback, bounded copy to HCI
 * transport. This callback owns the stack buffer and releases it once.
 * No codec, I2S, flash or NVRAM operation is performed here. */
static wiced_transport_buffer_pool_t *media_pool;
static uint32_t media_diag[8]={201};
void sp1_media_data(uint8_t handle,BT_HDR *pkt,uint32_t timestamp,uint8_t mpt){
 (void)handle;(void)timestamp;(void)mpt;++media_diag[1];
 if(!pkt){++media_diag[5];return;}
 uint32_t capacity=wiced_bt_get_buffer_size(pkt);
 uint32_t begin=sizeof(*pkt)+(uint32_t)pkt->offset;
 uint32_t len=pkt->len;
 if(capacity<sizeof(*pkt)||begin>capacity||len>capacity-begin||!len||len>1024){++media_diag[5];}
 else if(sp1_forward_ready(1)){
  media_diag[2]+=len;if(len>media_diag[6])media_diag[6]=len;
  uint8_t *payload=media_pool?wiced_transport_allocate_buffer(media_pool):NULL;
  uint32_t status=4;
  if(payload){memcpy(payload,((uint8_t*)pkt)+begin,len);
   /* Transport owns this allocation after send, including its error path. */
   status=wiced_transport_send_buffer(0x140a,payload,len);
  }
  media_diag[7]=status;if(status==WICED_SUCCESS)++media_diag[3];else ++media_diag[4];
 }
 wiced_bt_free_buffer(pkt);
}
#include "device_name.h"
static void publish_identity(void){
 wiced_bt_dev_set_local_name(SP1_BT_NAME);
 wiced_bt_dev_write_eir((uint8_t*)&sp1_inquiry,sizeof sp1_inquiry);
}
/* Consolidated relay report replaces old probe-only7e01/7e02 timer traffic. */
void sp1_audio_media_state(uint32_t *out){out[0]=media_diag[1];out[1]=media_diag[3];out[2]=media_diag[4];out[3]=media_diag[5];}
static volatile unsigned events, failures, configured;
static uint8_t incoming_peer;
extern int sp1_election_reserved(void);
int sp1_audio_is_peer(void){return incoming_peer;}
static wiced_bt_a2dp_codec_info_t codecs[]={{.codec_id=WICED_BT_A2DP_CODEC_SBC,.cie.sbc={
 A2D_SBC_IE_SAMP_FREQ_44|A2D_SBC_IE_SAMP_FREQ_48,
 A2D_SBC_IE_CH_MD_STEREO|A2D_SBC_IE_CH_MD_JOINT,
 A2D_SBC_IE_BLOCKS_16,A2D_SBC_IE_SUBBAND_8,A2D_SBC_IE_ALLOC_MD_L,53,2}}};
static wiced_bt_a2dp_config_data_t audio_config={.feature_mask=0,
 .codec_capabilities={1,codecs},.p_param={.buf_depth_ms=300,.start_buf_depth=50,.target_buf_depth=50}};
static uint8_t sdp[]={
 SDP_ATTR_SEQUENCE_1(79),SDP_ATTR_SEQUENCE_1(77),
 SDP_ATTR_RECORD_HANDLE(0x10001),SDP_ATTR_CLASS_ID(UUID_SERVCLASS_AUDIO_SINK),
 SDP_ATTR_ID(ATTR_ID_PROTOCOL_DESC_LIST),SDP_ATTR_SEQUENCE_1(16),
 SDP_ATTR_SEQUENCE_1(6),SDP_ATTR_UUID16(UUID_PROTOCOL_L2CAP),SDP_ATTR_VALUE_UINT2(BT_PSM_AVDTP),
 SDP_ATTR_SEQUENCE_1(6),SDP_ATTR_UUID16(UUID_PROTOCOL_AVDTP),SDP_ATTR_VALUE_UINT2(0x103),
 SDP_ATTR_ID(ATTR_ID_BT_PROFILE_DESC_LIST),SDP_ATTR_SEQUENCE_1(8),
 SDP_ATTR_SEQUENCE_1(6),SDP_ATTR_UUID16(UUID_SERVCLASS_ADV_AUDIO_DISTRIBUTION),SDP_ATTR_VALUE_UINT2(0x103),
 SDP_ATTR_UINT2(ATTR_ID_SUPPORTED_FEATURES,1),SDP_ATTR_SERVICE_NAME(16),
 'S','P','1',' ','A','u','d','i','o',' ','P','r','o','b','e',' '
};
extern int sp1_election_audio_peer(const uint8_t*);
static void audio_event(wiced_bt_a2dp_sink_event_t event,wiced_bt_a2dp_sink_event_data_t *d){
 if(event==WICED_BT_A2DP_SINK_CONNECT_EVT){
  if(d->connect.result==WICED_BT_SUCCESS){if(!sp1_election_audio_peer(d->connect.bd_addr)||!sp1_claim(1)){wiced_bt_a2dp_sink_disconnect(d->connect.handle);return;}incoming_peer=sp1_election_reserved();sp1_connection(1,0);}
 }
 if(event==WICED_BT_A2DP_SINK_DISCONNECT_EVT&&sp1_active_mode()==1)sp1_connection(0,0);
 if(event==WICED_BT_A2DP_SINK_START_IND_EVT&&sp1_active_mode()==1)sp1_connection(1,1);
 if(event==WICED_BT_A2DP_SINK_SUSPEND_EVT&&sp1_active_mode()==1)sp1_connection(1,0);
 ++events;
 if(event==WICED_BT_A2DP_SINK_CODEC_CONFIG_EVT){
  wiced_bt_a2dp_sink_route_config route={.route=AUDIO_ROUTE_COMPRESSED_TRANSPORT,.is_master=WICED_FALSE};
  configured=wiced_bt_a2dp_sink_update_route_config(d->codec_config.handle,&route);
  if(!configured)++failures;
 }
 if(event==WICED_BT_A2DP_SINK_START_IND_EVT)
  wiced_bt_a2dp_sink_send_start_response(d->start_ind.handle,d->start_ind.label,configured?WICED_SUCCESS:WICED_BT_ERROR);
 if(event==WICED_BT_A2DP_SINK_DISCONNECT_EVT)configured=0;
}
wiced_result_t sp1_audio_management(wiced_bt_management_evt_t e,wiced_bt_management_evt_data_t *d){

 if(e==BTM_ENABLED_EVT){

  if(d->enabled.status!=WICED_BT_SUCCESS)return WICED_BT_ERROR;
  /* Bypass unused opaque codec storage. SDK requires a transport pool
   * for payloads larger than 264 bytes. */
  media_pool=wiced_transport_create_buffer_pool(1024,6);

  if(!media_pool){++failures;return WICED_BT_ERROR;}

  if(wiced_bt_a2dp_sink_init(&audio_config,audio_event)!=WICED_SUCCESS){++failures;return WICED_BT_ERROR;}
  if(!wiced_bt_sdp_db_init(sdp,sizeof sdp))return WICED_BT_ERROR;
  publish_identity();
  wiced_bt_set_pairable_mode(1,0);
  wiced_bt_dev_set_discoverability(BTM_GENERAL_DISCOVERABLE,BTM_DEFAULT_DISC_WINDOW,BTM_DEFAULT_DISC_INTERVAL);
  wiced_bt_dev_set_connectability(WICED_TRUE,BTM_DEFAULT_CONN_WINDOW,BTM_DEFAULT_CONN_INTERVAL);
  return WICED_BT_SUCCESS;
 }
 if(e==BTM_PAIRING_IO_CAPABILITIES_BR_EDR_REQUEST_EVT){d->pairing_io_capabilities_br_edr_request.local_io_cap=BTM_IO_CAPABILITIES_NONE;d->pairing_io_capabilities_br_edr_request.oob_data=BTM_OOB_NONE;d->pairing_io_capabilities_br_edr_request.auth_req=BTM_AUTH_SINGLE_PROFILE_GENERAL_BONDING_NO;return WICED_BT_SUCCESS;}
 if(e==BTM_PAIRED_DEVICE_LINK_KEYS_REQUEST_EVT||e==BTM_LOCAL_IDENTITY_KEYS_REQUEST_EVT)return WICED_BT_ERROR; /* no saved keys in this nonpersistent probe */
 if(e==BTM_PAIRED_DEVICE_LINK_KEYS_UPDATE_EVT||e==BTM_LOCAL_IDENTITY_KEYS_UPDATE_EVT)return WICED_BT_SUCCESS;
 if(e==BTM_USER_CONFIRMATION_REQUEST_EVT){wiced_bt_dev_confirm_req_reply(WICED_BT_SUCCESS,d->user_confirmation_request.bd_addr);return WICED_BT_SUCCESS;}
 return WICED_BT_SUCCESS;
}

wiced_result_t sp1_audio_start(void){
 wiced_bt_management_evt_data_t d;memset(&d,0,sizeof d);d.enabled.status=WICED_BT_SUCCESS;
 wiced_result_t rc=sp1_audio_management(BTM_ENABLED_EVT,&d);

 return rc;
}
