#ifndef SP1_RELEASE_POLICY_H
#define SP1_RELEASE_POLICY_H
/* Protocol mode numbers remain compatible with308. No deferred feature modes. */
static int sr_select(unsigned mode){return mode==1||mode==4||mode==6||mode==7;}
static int sr_claim(unsigned mode){return mode==1||mode==4;}
static unsigned sr_handle_mode(unsigned handle){return handle>=0x20&&handle<=0x29?4:0;}
static int sr_channel(unsigned mode,unsigned opcode,unsigned n){return mode==4&&opcode==0x7e10&&n==32;}
#endif
