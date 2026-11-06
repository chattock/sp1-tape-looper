#ifndef BT239_LIFECYCLE_H
#define BT239_LIFECYCLE_H
/* Pure decisions shared with host tests. No flash operations. */
static inline unsigned bt_choice(unsigned mode,unsigned chosen){return mode==chosen?0:chosen;}
static inline int bt_can_reuse(int rx_on,int audio_busy){return !rx_on&&!audio_busy;}
/* Bit9 marks an explicit 8-bit track PWM level in bits16..23. */
static inline unsigned bt_breathe(unsigned t){
 /*610: same smoothstep shape as the empty-lane breath, scaled to the
  * existing BT peak255 and2.4s period. Flash lookup; no mutable storage. */
 static const unsigned char ease[17]={0,2,8,18,30,44,61,78,96,114,131,148,162,174,184,190,192};
 unsigned x=t%2400u; if(x>1200u)x=2400u-x;
 unsigned p=x*2u/3u,i=p/50u,f=p%50u;
 unsigned y=i>=16u?192u:ease[i]+(ease[i+1]-ease[i])*f/50u;
 unsigned level=(y*255u+96u)/192u;
 return 0x200u|(level<<16);
}
static inline unsigned bt_led_pattern(unsigned mode,unsigned state,unsigned age,unsigned now){
 /* state:1 waiting,2 looking,3 verified,4 failure. Track row is the gesture language. */
 if(state==4)return age<1000?(0x100|((age/125&1)?0xf0:0)):0;
 if(state==3)return age<1000?(0x100|(1u<<(4+age/250))):0;
 if(state==5)return age<1000?(0x100|(1u<<(7-age/250))):0;
 if(state==2)return 0x100|0xf0|bt_breathe(age);
 if(state==1&&mode==6)return 0x100|0xf0|bt_breathe(age);
 if(state==1)return 0x100|0xf0|bt_breathe(age);
 return 0;
}
#endif
