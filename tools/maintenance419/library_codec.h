/* Read-only export codec, matched against maintained browser decoder. */
static const int A7_STEP[]={7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,
  73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,
  408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,
  1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,
  7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,
  22385,24623,27086,29794,32767};
static const int A7_IDX7[]={-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 1,1,1,1,2,2,2,2,3,3,3,4,4,4,5,5,
  6,6,7,7,8,8,9,10,10,11,12,13,14,15,16,16};
static int16_t lc_clip(int v){return v>32767?32767:v< -32768?-32768:v;}
static int16_t lc_a7(int st[3],unsigned code){int pr=lc_clip(st[0]+((st[0]-st[1])>>1));int q=((code&63)*A7_STEP[st[2]]>>5)+A7_STEP[st[2]]/64;int v=lc_clip(pr+((code&64)?-q:q));st[1]=st[0];st[0]=v;int ix=st[2]+A7_IDX7[code&63];st[2]=ix<0?0:ix>88?88:ix;return v;}
static int16_t lc_s16(const uint8_t*p){return (int16_t)((unsigned)p[0]+((unsigned)p[1]<<8));}
static unsigned lc_bits(const uint8_t*p,unsigned bit,unsigned n,bool big){unsigned v=0;for(unsigned i=0;i<n;i++){unsigned b=bit+i;unsigned x=(p[b/8]>>(big?7-b%8:b%8))&1;v=big?(v<<1)|x:v|(x<<i);}return v;}
static int16_t lc_p14(const uint8_t*b,unsigned sample){
 unsigned bit=sample*14,byte=bit/8,shift=bit%8;const uint8_t*p=b+16+byte;
 uint32_t word=((uint32_t)p[0]<<16)|((uint32_t)p[1]<<8)|p[2];
 int v=(word>>(10-shift))&16383;if(v>=8192)v-=16384;return (int16_t)((v*4)>>(b[14]&3));
}
static bool lc_decode(const uint8_t*b,unsigned spb,int16_t*out){
 memset(out,0,spb*4);
 if(b[11]==0x5b){
  if((b[2]==0x36?496u:280u)!=spb)return false; /* reject mixed-stride corruption */
  if(b[2]==0x36){for(unsigned i=0;i<248;i++){int a=lc_s16(b+16+2*i),z=i<247?lc_s16(b+18+2*i):a;out[4*i]=out[4*i+1]=a;out[4*i+2]=out[4*i+3]=(a+z+1)>>1;}}
  else if(b[2]==0x34){for(unsigned i=0;i<140;i++)for(unsigned ch=0;ch<2;ch++){int a=lc_p14(b,i*2+ch),z=i<139?lc_p14(b,(i+1)*2+ch):a;out[4*i+ch]=a;out[4*i+2+ch]=(a+z)>>1;}}
  return true;
 }
 if(spb==496)return true; /* native headerless silence */
 int l[3]={lc_s16(b),lc_s16(b+2),b[4]>88?88:b[4]},r[3]={lc_s16(b+6),lc_s16(b+8),b[10]>88?88:b[10]};bool stereo=b[5]&1;
 for(unsigned i=0;i<280;i++){unsigned c=lc_bits(b+16,i*(stereo?14:7),7,false);out[2*i]=lc_a7(l,c);out[2*i+1]=stereo?lc_a7(r,lc_bits(b+16,i*14+7,7,false)):out[2*i];}return true;
}
/* The next marked block heals the preceding interpolation hold. No preceding
 * block is needed, so arbitrary/repeated host reads are deterministic. */
static bool lc_heal(const uint8_t*next,unsigned spb,int16_t*out){
 if(next[11]!=0x5b)return true;
 if((next[2]==0x36?496u:280u)!=spb)return false;
 for(unsigned ch=0;ch<2;ch++){int v=next[2]==0x36?lc_s16(next+16):next[2]==0x34?lc_p14(next,ch):0;out[(spb-1)*2+ch]=(out[(spb-2)*2+ch]+v)>>1;}
 return true;
}
