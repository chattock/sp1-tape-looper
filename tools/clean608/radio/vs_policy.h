#pragma once
/* Owned flat records only, after exact source application admission. Legacy or
 * inconsistent binding clears BOTH key sets. Other records remain byte-exact. */
static uint8_t expected_vs[4096];
static bool vs_target(void){
 memcpy(expected_vs,original_vs,4096);unsigned at=0,pos[2]={4096,4096};
 bool seen[124]={0},keep[2]={0},valid[2]={0},repair=false;
 while(at+4<=4096){
  unsigned tag=original_vs[at],n=original_vs[at+1],check=original_vs[at+2],hdr=original_vs[at+3];
  if(tag==255){for(unsigned i=at;i<4096;i++)if(original_vs[i]!=255)return false;break;}
  unsigned next=at+4+((n+1)&~1u);if(!n||next>4096)return false;
  unsigned sum=0;for(unsigned i=0;i<n;i++)sum+=original_vs[at+4+i];if(((sum^0x55)&255)!=check)return false;
  if(tag){
   unsigned id=tag&127;
   if(!(tag&128)||id<1||id>0x7b||seen[id]||(((tag+n+check)^0x55)&255)!=hdr)return false;
   seen[id]=true;
   if(tag==0xf0||tag==0xf1){unsigned kind=tag-0xf0,size=kind?65:138;const uint8_t*r=original_vs+at+4;pos[kind]=at;
    bool old=n==156&&!memcmp(r,"BND1",4)&&r[4]==1;
    bool current=n==180&&!memcmp(r,"TLB2",4)&&r[4]==2;
    if((!old&&!current)||r[5]!=kind||r[6]!=size||r[7]>1)return false;
    uint32_t crc=(uint32_t)r[n-4]|(uint32_t)r[n-3]<<8|(uint32_t)r[n-2]<<16|(uint32_t)r[n-1]<<24;
    if(sg_crc(r,n-4)!=crc)return false;
    unsigned start=old?8:32;
    for(unsigned i=start+size;i<n-4;i++)if(r[i])return false;
    if(current&&(r[30]||r[31]))return false;
    if(current){bool zero=true,ff=true;for(unsigned i=8;i<14;i++){zero&=r[i]==0;ff&=r[i]==255;}if(zero||ff)return false;}
    valid[kind]=r[7];keep[kind]=current&&!memcmp(r+8,local_address,6)&&!memcmp(r+14,target_abi,16);
    if(!keep[kind])repair=true;
   }
  }
  at=next;
 }
 if(at+4>4096)return false;
 if(valid[1]&&!valid[0])repair=true;
 if(repair)keep[0]=keep[1]=false;
 for(unsigned kind=0;kind<2;kind++){
  if(keep[kind])continue;
  if(at+184>=4093)return false;
  if(pos[kind]<4096)expected_vs[pos[kind]]=0; /* ROM tombstone: tag only. */
  uint8_t*r=expected_vs+at+4;memset(r,0,180);memcpy(r,"TLB2",4);r[4]=2;r[5]=kind;r[6]=kind?65:138;
  memcpy(r+8,local_address,6);memcpy(r+14,target_abi,16);uint32_t crc=sg_crc(r,176);for(unsigned i=0;i<4;i++)r[176+i]=crc>>(8*i);
  unsigned sum=0;for(unsigned i=0;i<180;i++)sum+=r[i];unsigned check=(sum^0x55)&255,tag=0xf0+kind;
  expected_vs[at]=tag;expected_vs[at+1]=180;expected_vs[at+2]=check;expected_vs[at+3]=((tag+180+check)^0x55)&255;at+=184;
 }
 return true;
}
