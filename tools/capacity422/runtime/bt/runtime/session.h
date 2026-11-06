/* Project-authored connection-scoped handshake; nonce generation is external.
 * Reset on an actual link-generation change, not on receipt of a HELLO. */
#ifndef SP1_LINK_SESSION_H
#define SP1_LINK_SESSION_H
#include "transport.h"
enum {SP1_SESSION_HELLO=3,SP1_SESSION_READY=4,SP1_SESSION_WAIT=1,SP1_SESSION_UP=2,SP1_SESSION_FAILED=3};
typedef struct {uint32_t local,remote,previous_local,previous_remote,started,last;uint8_t state,sent,ready_sent,ready_received;} sp1_session;
static inline int sp1_session_start(sp1_session *s,uint32_t nonce,uint32_t now){
 if(!nonce||nonce==s->local||nonce==s->previous_local)return 0;
 uint32_t old=s->local,peer=s->remote?s->remote:s->previous_remote;
 memset(s,0,sizeof *s);s->previous_local=old;s->previous_remote=peer;s->local=nonce;s->started=now;s->state=SP1_SESSION_WAIT;return 1;
}
static inline int sp1_session_receive(sp1_session *s,const uint8_t *b,size_t n){
 if((s->state!=SP1_SESSION_WAIT&&s->state!=SP1_SESSION_UP)||!b||n!=32||b[0]!='S'||b[1]!='L'||b[2]!=1||
 (b[3]!=SP1_SESSION_HELLO&&b[3]!=SP1_SESSION_READY)||sp1_link_crc(b,30)!=((uint16_t)b[30]|((uint16_t)b[31]<<8)))return 0;
 for(unsigned i=12;i<30;i++)if(b[i])return 0;
 uint32_t peer=sp1_link_get(b+4),target=sp1_link_get(b+8);
 if(!peer||peer==s->previous_remote||(s->remote&&s->remote!=peer))return 0;
 if(b[3]==SP1_SESSION_HELLO){if(target)return 0;}
 else if(target!=s->local)return 0;
 s->remote=peer;
 if(b[3]==SP1_SESSION_READY)s->ready_received=1;
 /* A duplicate HELLO is not permission to abandon an established session. */
 if(s->ready_received&&s->ready_sent)s->state=SP1_SESSION_UP;
 return 1;
}
static inline int sp1_session_poll(sp1_session *s,uint32_t now,uint8_t b[32]){
 if(s->state!=SP1_SESSION_WAIT&&s->state!=SP1_SESSION_UP)return 0;
 if(s->state==SP1_SESSION_WAIT&&(uint32_t)(now-s->started)>=5000){s->state=SP1_SESSION_FAILED;return 0;}
 /* Keep replying READY after local establishment until peer delivery is proven.
  * Adapter stops calling once a valid peer DATA/ACK establishes that proof. */
 if(s->sent&&(uint32_t)(now-s->last)<(s->state==SP1_SESSION_UP?500u:200u))return 0;
 memset(b,0,32);b[0]='S';b[1]='L';b[2]=1;b[3]=s->remote?SP1_SESSION_READY:SP1_SESSION_HELLO;
 sp1_link_put(b+4,s->local);sp1_link_put(b+8,s->remote);
 uint16_t crc=sp1_link_crc(b,30);b[30]=crc;b[31]=crc>>8;s->sent=1;s->last=now;
 if(s->remote)s->ready_sent=1;
 if(s->ready_received&&s->ready_sent)s->state=SP1_SESSION_UP;
 return 1;
}
#endif
