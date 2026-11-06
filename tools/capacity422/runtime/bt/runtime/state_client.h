/* Project-authored snapshot: legacy peer v4 (56 bytes); companion v6 (448 bytes).
 * Companion deltas carry only changed 8-byte blocks after a complete baseline.
 * Session freshness is NOT authentication. No remote writes or MIDI. */
#ifndef SP1_STATE_CLIENT_H
#define SP1_STATE_CLIENT_H
#include "session.h"
#define STATE_BYTES 12
#define RICH_BYTES 448
#define RICH_PARTS 56
#define STATE_FRAME_LIMIT 96
static inline uint32_t state_hash(uint32_t h,const uint8_t *p){for(unsigned i=0;i<12;i++)h=(h^p[i])*16777619u;return h;}
static inline int state_identity_valid(const uint8_t *p){
 unsigned any=0,all=255;for(unsigned i=48;i<56;i++){any|=p[i];all&=p[i];}return any&&all!=255;
}
static inline int state_valid(const uint8_t *p){return p[0]==0x11&&p[1]==1&&!(p[2]&~3u)&&p[5]<=15&&p[10]<=8&&!p[11];}
/* Coverage is counted on the exact accepted/sent payloads, not UI sampling. */
typedef struct {uint32_t transitions[4],seen_play;uint8_t previous[12],have;} state_coverage;
static inline void state_observe(state_coverage *c,const uint8_t *p){
 c->seen_play|=1u<<(p[2]&1u);
 if(c->have){
  c->transitions[0]+=!!((p[2]^c->previous[2])&1u);
  c->transitions[1]+=!!((p[3]^c->previous[3])&0xf0u);
  c->transitions[2]+=p[5]!=c->previous[5];
  c->transitions[3]+=memcmp(p+6,c->previous+6,4)!=0;
 }
 memcpy(c->previous,p,12);c->have=1;
}
typedef struct {
 sp1_session session;sp1_link link;state_coverage outgoing,incoming;
 uint32_t stale,sent,received,tx_hash,rx_hash,rx_playing,rx_changes,errors,last_send,last_rx,poll_at,poll_gap,ack_ms_sum,ack_ms_max;
 uint8_t linked,proven,remote[12];
 uint8_t assembling[RICH_BYTES],remote_full[RICH_BYTES],out_full[RICH_BYTES],last_sent[RICH_BYTES];
 uint8_t tx_part,rx_part,tx_revision,rx_revision,remote_valid;
 uint64_t tx_mask,rx_mask;
 uint32_t snapshot_at,full_sent,full_received,level_changes,rx_level_changes,page_seen,rx_page_seen;
 uint16_t previous_level[4],previous_rx_level[4],initial_level[4],initial_rx_level[4];
 uint8_t down_mask,return_mask,rx_down_mask,rx_return_mask;
 uint8_t fx_min[16],fx_max[16],rx_fx_min[16],rx_fx_max[16];
 uint8_t page_pick,track_pick,rx_page_pick,rx_track_pick;
} state_client;
static inline int state_begin(state_client *c,uint32_t seed,uint32_t now){
 memset(c,0,sizeof *c);c->tx_hash=c->rx_hash=2166136261u;
 return sp1_session_start(&c->session,seed,now);
}
static inline int state_reconnect(state_client *c,uint32_t seed,uint32_t now){
 sp1_session previous=c->session;
 memset(c,0,sizeof *c);c->session=previous;c->tx_hash=c->rx_hash=2166136261u;
 return sp1_session_start(&c->session,seed,now);
}
static inline void state_receive(state_client *c,const uint8_t *p,uint32_t now){
 if(p[3]==SP1_SESSION_HELLO||p[3]==SP1_SESSION_READY){sp1_session_receive(&c->session,p,32);return;}
 if(!c->linked)return;
 /* Only classify well-formed CRC-checked old-session frames as stale.
  * Stale data cannot update state or provoke an ACK in the new session. */
 if((p[3]==SP1_LINK_DATA||p[3]==SP1_LINK_ACK)&&p[0]=='S'&&p[1]=='L'&&p[2]==1&&
    sp1_link_crc(p,30)==((uint16_t)p[30]|((uint16_t)p[31]<<8))&&
    (sp1_link_get(p+4)!=c->session.remote||sp1_link_get(p+8)!=c->session.local)){++c->stale;return;}
 /* A retransmitted DATA frame can produce another ACK after the first ACK
  * advanced tx_sequence. Ignore ONLY fully validated older acknowledgments.
  * Future/malformed/wrong-epoch ACKs still fail the ordinary parser. */
 if(p[0]=='S'&&p[1]=='L'&&p[2]==1&&p[3]==SP1_LINK_ACK&&
    sp1_link_get(p+4)==c->session.remote&&sp1_link_get(p+8)==c->session.local&&
    sp1_link_crc(p,30)==((uint16_t)p[30]|((uint16_t)p[31]<<8))&&
    sp1_link_get(p+12)>0&&sp1_link_get(p+12)<c->link.tx_sequence){
  unsigned shape=0;for(unsigned i=16;i<30;i++)shape|=p[i];
  if(!shape){++c->link.late_ack;return;}
 }
 uint32_t ack=c->link.acknowledged,at=c->link.first_at;
 if(sp1_link_receive(&c->link,p,32))c->proven=1;
 if(c->link.acknowledged!=ack){uint32_t age=now-at;c->ack_ms_sum+=age;if(age>c->ack_ms_max)c->ack_ms_max=age;}
 sp1_link_message m;
 if(sp1_link_take(&c->link,&m)){
  bool delta=m.data[0]==0x13&&m.data[1]==5;
  unsigned part=m.data[2]&0x7f;bool final=delta?!!(m.data[2]&0x80):part==6;
  if(m.length!=12||(!delta&&(m.data[0]!=0x12||m.data[1]!=4))||part>=7||(!delta&&m.data[2]>=7)){++c->errors;return;}
  if((delta?part<c->rx_part:part!=c->rx_part)||m.data[3]!=(uint8_t)c->full_received){++c->errors;return;}
  if(!c->rx_part){c->rx_revision=m.data[3];c->rx_mask=0;memcpy(c->assembling,c->remote_full,RICH_BYTES);}
  memcpy(c->assembling+part*8,m.data+4,8);c->rx_part=part+1;c->rx_mask|=1u<<part;
  c->rx_hash=state_hash(c->rx_hash,m.data);++c->received;
  if(final){
   if(!c->full_received&&c->rx_mask!=127){++c->errors;return;}
   if(!state_valid(c->assembling)||!state_identity_valid(c->assembling)||c->assembling[44]>15||c->assembling[45]>15||c->assembling[46]>15||c->assembling[47]){++c->errors;return;}
   for(unsigned i=0;i<4;i++){
    uint16_t v=c->assembling[12+2*i]|((uint16_t)c->assembling[13+2*i]<<8);
    uint16_t physical=c->assembling[20+2*i]|((uint16_t)c->assembling[21+2*i]<<8);
    if(v>256||physical>4095){++c->errors;return;}
    if(c->full_received&&v!=c->previous_rx_level[i])++c->rx_level_changes;
    if(!c->full_received)c->initial_rx_level[i]=v;
    int d=(int)c->initial_rx_level[i]-(int)v;
    if(d>=32)c->rx_down_mask|=UINT64_C(1)<<i;
    if((c->rx_down_mask&(1u<<i))&&d>=-8&&d<=8)c->rx_return_mask|=UINT64_C(1)<<i;
    c->previous_rx_level[i]=v;
   }
   for(unsigned i=0;i<16;i++){unsigned v=c->assembling[28+i];
    if(!c->full_received||v<c->rx_fx_min[i])c->rx_fx_min[i]=v;
    if(!c->full_received||v>c->rx_fx_max[i])c->rx_fx_max[i]=v;
   }
   c->rx_page_pick|=c->assembling[44];c->rx_track_pick|=c->assembling[46];
   if(c->full_received&&memcmp(c->remote,c->assembling,12))++c->rx_changes;
   state_observe(&c->incoming,c->assembling);memcpy(c->remote,c->assembling,12);
   memcpy(c->remote_full,c->assembling,RICH_BYTES);c->remote_valid=1;c->rx_part=0;++c->full_received;
   c->rx_playing+=7*!!(c->remote[2]&1);c->last_rx=now;c->rx_page_seen|=1u<<c->remote[10];
  }
 }
}
static inline int state_poll_paced(state_client *c,uint32_t now,const uint8_t state[RICH_BYTES],bool publish,uint8_t out[32],uint32_t interval_ms){
 if(c->remote_valid&&now-c->last_rx>3000){c->remote_valid=0;memset(c->remote_full,0,RICH_BYTES);memset(c->remote,0,12);}
 if(c->poll_at&&now-c->poll_at>c->poll_gap)c->poll_gap=now-c->poll_at;
 c->poll_at=now;
 if(c->errors||c->session.state==SP1_SESSION_FAILED||c->link.state==SP1_LINK_FAILED)return -1;
 if(c->session.state==SP1_SESSION_UP&&!c->linked){if(!sp1_link_begin(&c->link,c->session.local,c->session.remote))return -1;c->linked=1;c->link.retry_ms=1200;}
 if(c->linked){
  /* Only one outstanding snapshot; no stale backlog and no blocking producer. */
  if(publish&&!c->link.tx_count&&(!c->sent||now-c->last_send>=interval_ms)){
   bool delta=interval_ms==10;
   if(!c->tx_mask){
    if(delta&&c->full_sent&&now-c->snapshot_at<50)goto poll_link;
    if(!state_valid(state)||!state_identity_valid(state))return -1;
    uint64_t mask=delta?((UINT64_C(1)<<RICH_PARTS)-1):127;
    if(delta&&c->full_sent){mask=0;for(unsigned i=0;i<RICH_PARTS;i++)if(memcmp(state+i*8,c->last_sent+i*8,8))mask|=UINT64_C(1)<<i;
     if(!mask){if(now-c->last_send<500)goto poll_link;mask=1;}}
    memcpy(c->out_full,state,RICH_BYTES);c->tx_mask=mask;c->snapshot_at=now;
   }
   unsigned index=0;while(!(c->tx_mask&(UINT64_C(1)<<index)))++index;
   bool final=(c->tx_mask&~(UINT64_C(1)<<index))==0;
   uint8_t part[12]={delta?0x14:0x12,delta?6:4,(uint8_t)(index|(delta&&final?128:0)),c->tx_revision};memcpy(part+4,c->out_full+index*8,8);
   if(sp1_link_send(&c->link,part,12)){
    ++c->sent;c->tx_hash=state_hash(c->tx_hash,part);c->last_send=now;c->tx_mask&=~(UINT64_C(1)<<index);c->tx_part=index+1;
    if(final){
     c->tx_part=0;++c->tx_revision;state_observe(&c->outgoing,c->out_full);
     for(unsigned i=0;i<4;i++){
      uint16_t v=c->out_full[12+2*i]|((uint16_t)c->out_full[13+2*i]<<8);
      if(c->full_sent&&v!=c->previous_level[i])++c->level_changes;
      if(!c->full_sent)c->initial_level[i]=v;
      int d=(int)c->initial_level[i]-(int)v;
      if(d>=32)c->down_mask|=UINT64_C(1)<<i;
      if((c->down_mask&(1u<<i))&&d>=-8&&d<=8)c->return_mask|=UINT64_C(1)<<i;
      c->previous_level[i]=v;
     }
     for(unsigned i=0;i<16;i++){unsigned v=c->out_full[28+i];
      if(!c->full_sent||v<c->fx_min[i])c->fx_min[i]=v;
      if(!c->full_sent||v>c->fx_max[i])c->fx_max[i]=v;
     }
     c->page_pick|=c->out_full[44];c->track_pick|=c->out_full[46];
     memcpy(c->last_sent,c->out_full,RICH_BYTES);++c->full_sent;c->page_seen|=1u<<c->out_full[10];
    }
   }
  }
 poll_link:
  if(sp1_link_poll(&c->link,now,out))return 1;
  if(c->link.state==SP1_LINK_FAILED)return -1;
 }
 if(!c->proven&&sp1_session_poll(&c->session,now,out))return 1;
 return c->session.state==SP1_SESSION_FAILED?-1:0;
}
/* Tested peer pacing stays unchanged; companion opts into bounded faster service. */
static inline int state_poll(state_client *c,uint32_t now,const uint8_t state[RICH_BYTES],bool publish,uint8_t out[32]){return state_poll_paced(c,now,state,publish,out,250);}
#endif
