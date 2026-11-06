#include "handoff.h"
#define ACTIVE 1u
#define REQUEST 2u
#define AUDIO 4u
#define IO 8u
#define READY 16u
#define FAILED 32u
#define LOW 255u
#define TICKET_SHIFT 12u
static unsigned read_word(const song_handoff *h){return atomic_load_explicit(&h->word,memory_order_acquire);}
void song_handoff_init(song_handoff *h){atomic_init(&h->word,ACTIVE);}
uint32_t song_handoff_request(song_handoff *h,unsigned song,bool quiet){
 if(!quiet||song>=16)return 0;
 unsigned old=read_word(h),phase=old&LOW,epoch=old>>TICKET_SHIFT;
 if((phase!=ACTIVE&&phase!=FAILED)||epoch==0xfffffu)return 0;
 unsigned next=((epoch+1)<<TICKET_SHIFT)|(song<<8)|REQUEST;
 return atomic_compare_exchange_strong_explicit(&h->word,&old,next,memory_order_acq_rel,memory_order_acquire)?epoch+1:0;
}
bool song_handoff_ack(song_handoff *h,uint32_t ticket,bool audio){
 if(!ticket||ticket>0xfffffu)return false;
 unsigned old=read_word(h),bit=audio?AUDIO:IO;
 /* Two bounded attempts handle the other actor's simultaneous acknowledgment.
  * A later callback may retry; audio never spins waiting for storage. */
 for(unsigned attempt=0;attempt<2;attempt++){
  if(old>>TICKET_SHIFT!=ticket||!(old&REQUEST)||(old&(READY|FAILED)))return false;
  if(old&bit)return true;
  if(atomic_compare_exchange_strong_explicit(&h->word,&old,old|bit,memory_order_acq_rel,memory_order_acquire))return true;
 }
 return false;
}
bool song_handoff_drained(const song_handoff *h,uint32_t ticket,unsigned *song){
 unsigned w=read_word(h);
 if(!ticket||w>>TICKET_SHIFT!=ticket||(w&LOW)!=(REQUEST|AUDIO|IO))return false;
 if(song)*song=(w>>8)&15;return true;
}
bool song_handoff_publish(song_handoff *h,uint32_t ticket,bool success){
 unsigned old=read_word(h);
 if(!ticket||old>>TICKET_SHIFT!=ticket||(old&LOW)!=(REQUEST|AUDIO|IO))return false;
 unsigned next=(old&~LOW)|(success?READY:FAILED);
 return atomic_compare_exchange_strong_explicit(&h->word,&old,next,memory_order_release,memory_order_relaxed);
}
bool song_handoff_accept(song_handoff *h,uint32_t ticket){
 unsigned old=read_word(h);
 if(!ticket||old>>TICKET_SHIFT!=ticket||(old&LOW)!=READY)return false;
 return atomic_compare_exchange_strong_explicit(&h->word,&old,(old&~LOW)|ACTIVE,memory_order_acq_rel,memory_order_acquire);
}
uint32_t song_handoff_pending(const song_handoff *h){unsigned w=read_word(h);return (w&(REQUEST|READY))?w>>TICKET_SHIFT:0;}
bool song_handoff_playable(const song_handoff *h){return (read_word(h)&LOW)==ACTIVE;}

bool song_handoff_ready(const song_handoff *h,uint32_t ticket){
 unsigned w=read_word(h);return ticket&&w>>TICKET_SHIFT==ticket&&(w&LOW)==READY;
}
