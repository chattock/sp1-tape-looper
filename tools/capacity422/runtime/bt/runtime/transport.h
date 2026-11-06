/* Project-authored, SDK-independent reliable message core. No heap, IO or clocks.
 * Single owner only: callbacks/ISRs must marshal events onto the owning thread.
 * ACK means accepted into the remote controller queue, NOT applied to playback.
 * Epoch establishment/authentication and BLE fragmentation belong to adapters. */
#ifndef SP1_LINK_TRANSPORT_H
#define SP1_LINK_TRANSPORT_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
enum { SP1_LINK_BYTES=32, SP1_LINK_PAYLOAD=12, SP1_LINK_QUEUE=4,
       SP1_LINK_DATA=1, SP1_LINK_ACK=2, SP1_LINK_RETRY_MS=600,
       SP1_LINK_TIMEOUT_MS=6000, SP1_LINK_MAX_ATTEMPTS=10 };
enum { SP1_LINK_OFF, SP1_LINK_UP, SP1_LINK_FAILED, SP1_LINK_EXHAUSTED };
typedef struct { uint8_t length, data[SP1_LINK_PAYLOAD]; } sp1_link_message;
typedef struct {
 uint32_t retry_ms;
 uint32_t bad_header,bad_padding,bad_shape,late_ack,bad_ack,changed_duplicate,bad_sequence;
 uint32_t local_epoch,remote_epoch,tx_sequence,rx_sequence;
 uint32_t first_at,last_at,accepted,acknowledged,duplicates,invalid,backpressure,retries,cancelled;
 uint8_t state,tx_head,tx_count,rx_head,rx_count,attempts,inflight,ack_pending;
 sp1_link_message tx[SP1_LINK_QUEUE],rx[SP1_LINK_QUEUE],last_received;
} sp1_link;
static inline uint16_t sp1_link_crc(const uint8_t *p,size_t n){
 uint16_t c=0xffff;
 for(size_t i=0;i<n;i++){c^=(uint16_t)p[i]<<8;for(unsigned j=0;j<8;j++)c=(uint16_t)((c<<1)^((c&0x8000)?0x1021:0));}return c;
}
static inline void sp1_link_put(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static inline uint32_t sp1_link_get(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
/* Fresh, nonzero per-endpoint epochs must be established by the adapter.
 * Zero-initialize once. Do not reuse an epoch after reboot/reconnect. */
static inline int sp1_link_begin(sp1_link *s,uint32_t local,uint32_t remote){
 if(!local||!remote||local==s->local_epoch||remote==s->remote_epoch)return 0;
 uint32_t cancelled=s->cancelled+s->tx_count+s->rx_count;
 memset(s,0,sizeof *s);s->local_epoch=local;s->remote_epoch=remote;s->tx_sequence=1;s->state=SP1_LINK_UP;s->cancelled=cancelled;return 1;
}
static inline void sp1_link_end(sp1_link *s){
 s->cancelled+=s->tx_count+s->rx_count;s->tx_count=s->rx_count=s->inflight=s->ack_pending=0;s->state=SP1_LINK_OFF;
}
static inline int sp1_link_send(sp1_link *s,const void *data,size_t n){
 if(s->state!=SP1_LINK_UP||!data||!n||n>SP1_LINK_PAYLOAD)return 0;
 if(s->tx_count==SP1_LINK_QUEUE){++s->backpressure;return 0;}
 sp1_link_message *m=&s->tx[(s->tx_head+s->tx_count)%SP1_LINK_QUEUE];memset(m,0,sizeof *m);m->length=(uint8_t)n;memcpy(m->data,data,n);++s->tx_count;return 1;
}
static inline void sp1_link_frame(sp1_link *s,uint8_t *b,unsigned type,uint32_t sequence,const sp1_link_message *m){
 memset(b,0,SP1_LINK_BYTES);b[0]='S';b[1]='L';b[2]=1;b[3]=(uint8_t)type;
 sp1_link_put(b+4,s->local_epoch);sp1_link_put(b+8,s->remote_epoch);sp1_link_put(b+12,sequence);
 if(m){b[16]=m->length;memcpy(b+18,m->data,m->length);}
 uint16_t c=sp1_link_crc(b,30);b[30]=(uint8_t)c;b[31]=(uint8_t)(c>>8);
}
/* Produces at most one frame. Caller owns copying/sending it. Retry handles loss.
 * now is a wrapping millisecond clock; call regularly (<2^31 ms apart). */
static inline int sp1_link_poll(sp1_link *s,uint32_t now,uint8_t b[SP1_LINK_BYTES]){
 if(s->state!=SP1_LINK_UP)return 0;
 if(s->inflight&&(uint32_t)(now-s->first_at)>=(s->retry_ms==1200?12000u:SP1_LINK_TIMEOUT_MS)){s->state=SP1_LINK_FAILED;return 0;}
 if(s->ack_pending){sp1_link_frame(s,b,SP1_LINK_ACK,s->rx_sequence,0);s->ack_pending=0;return 1;}
 if(!s->tx_count)return 0;
 if(s->inflight&&(uint32_t)(now-s->last_at)<(s->retry_ms?s->retry_ms:SP1_LINK_RETRY_MS))return 0;
 if(s->attempts>=SP1_LINK_MAX_ATTEMPTS){s->state=SP1_LINK_FAILED;return 0;}
 if(!s->inflight){s->first_at=now;s->inflight=1;}else ++s->retries;
 ++s->attempts;s->last_at=now;sp1_link_frame(s,b,SP1_LINK_DATA,s->tx_sequence,&s->tx[s->tx_head]);return 1;
}
/* No ACK for malformed/stale/out-of-order messages or a full receive queue. */
static inline int sp1_link_receive(sp1_link *s,const uint8_t *b,size_t n){
 if(s->state!=SP1_LINK_UP)return 0;
 if(!b||n!=SP1_LINK_BYTES||b[0]!='S'||b[1]!='L'||b[2]!=1||b[17]||
    (b[3]!=SP1_LINK_DATA&&b[3]!=SP1_LINK_ACK)||b[16]>SP1_LINK_PAYLOAD||
    sp1_link_get(b+4)!=s->remote_epoch||sp1_link_get(b+8)!=s->local_epoch||
    sp1_link_crc(b,30)!=((uint16_t)b[30]|((uint16_t)b[31]<<8))){++s->bad_header;++s->invalid;return 0;}
 unsigned length=b[16];uint32_t seq=sp1_link_get(b+12);
 for(unsigned i=length;i<SP1_LINK_PAYLOAD;i++)if(b[18+i]){++s->bad_padding;++s->invalid;return 0;}
 if(!seq||(b[3]==SP1_LINK_ACK?length!=0:length==0)){++s->bad_shape;++s->invalid;return 0;}
 if(b[3]==SP1_LINK_ACK){
  if(!s->inflight||!s->tx_count||seq!=s->tx_sequence){if(seq<s->tx_sequence)++s->late_ack;else ++s->bad_ack;++s->invalid;return 0;}
  s->tx_head=(uint8_t)((s->tx_head+1)%SP1_LINK_QUEUE);--s->tx_count;s->attempts=s->inflight=0;++s->acknowledged;
  if(s->tx_sequence==UINT32_MAX)s->state=SP1_LINK_EXHAUSTED;else ++s->tx_sequence;
  return 1;
 }
 if(seq==s->rx_sequence){
  if(length!=s->last_received.length||memcmp(b+18,s->last_received.data,length)){++s->changed_duplicate;++s->invalid;return 0;}
  ++s->duplicates;s->ack_pending=1;return 1;
 }
 if(s->rx_sequence==UINT32_MAX||seq!=s->rx_sequence+1){++s->bad_sequence;++s->invalid;return 0;}
 if(s->rx_count==SP1_LINK_QUEUE){++s->backpressure;return 0;}
 sp1_link_message *m=&s->rx[(s->rx_head+s->rx_count)%SP1_LINK_QUEUE];memset(m,0,sizeof *m);m->length=(uint8_t)length;memcpy(m->data,b+18,length);
 s->last_received=*m;++s->rx_count;s->rx_sequence=seq;++s->accepted;s->ack_pending=1;return 1;
}
static inline int sp1_link_take(sp1_link *s,sp1_link_message *m){
 if(!m||!s->rx_count)return 0;
 *m=s->rx[s->rx_head];s->rx_head=(uint8_t)((s->rx_head+1)%SP1_LINK_QUEUE);--s->rx_count;return 1;
}
#endif
