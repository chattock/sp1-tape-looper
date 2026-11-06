#include "controls.h"
#include "geometry.h"
#include <string.h>
static uint32_t get(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static unsigned get16(const uint8_t *p){return p[0]|(unsigned)p[1]<<8;}
static void put(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static void put16(uint8_t *p,unsigned v){p[0]=v;p[1]=v>>8;}
static unsigned sum(const uint8_t *p,unsigned n){unsigned v=0;for(unsigned i=0;i<n;i++)v=(v+p[i])&65535;return v;}
static void extension(uint8_t *p,uint32_t magic,unsigned n){
 if(get(p)!=magic||get16(p+4+n)!=sum(p+4,n)){memset(p,0,n+6);put(p,magic);}
}
bool music_controls_track(uint8_t c[3072],unsigned song,unsigned track,const take_metadata *m){
 if(!c||song>=16||track>=4||get(c)!=0x53453341||get(c+4)>=16)return false;
 uint8_t *row=c+8+song*44,*x=c+1536,*entry=x+16+(song*4+track)*16;
 if(get(x)!=0x53453358||get16(x+4)!=1||get16(x+6)!=sum(x+16,1024))return false;
 bool others=false;for(unsigned i=0;i<4;i++){if(row[8+i]>1)return false;if(i!=track&&row[8+i])others=true;}
 uint32_t blocks=0,song_frames=get(row+4);
 if(m){
  if(!take_valid(m)||!m->loop_blocks||m->track_head_blocks>65535||m->track_tail_blocks>65535)return false;
  blocks=m->loop_blocks;
  if(!others&&!song_frames)song_frames=m->song_frames?m->song_frames:m->frames;
  music_geometry g;
  if(!music_geometry_load(m->frames_per_block,song_frames,blocks,m->start_frames/m->frames_per_block,
      m->frames,m->start_frames,true,m->audio_blocks,&g)||g.frames!=m->frames||g.start_frames!=m->start_frames)return false;
 }
 /* No failure paths after mutation begins. */
 row[8+track]=m?1:0;put(row+12+track*4,blocks);put(row+28+track*4,m?m->start_frames/m->frames_per_block:0);
 put(c+716+(song*4+track)*4,m?m->content_blocks:0);
 c[1008+song]=(c[1008+song]&~(0x10u<<track))|(m&&m->muted?(0x10u<<track):0);
 if(m){
  put(entry,m->start_frames);put(entry+4,m->frames);put(entry+8,m->content_blocks);
  entry[12]=m->format==2?6:5;entry[13]=m->content_flags;entry[14]=m->pan;entry[15]=0x80u|(m->format==2);
 }else{put(entry,0);put(entry+4,0);put(entry+8,0);entry[12]=0;entry[13]=0;}
 put16(x+6,sum(x+16,1024));
 uint8_t *tr=c+2576;
 if(get(tr)!=0x324d5254||get16(tr+6)!=sum(tr+8,384)){memset(tr,0,392);put(tr,0x324d5254);put16(tr+4,1);}
 put16(tr+136+(song*4+track)*2,m?m->track_head_blocks:0);
 put16(tr+264+(song*4+track)*2,m?m->track_tail_blocks:0);
 uint8_t *g=c+1024,*g2=g+256,*g3=g+368,*g4=g+456;
 extension(g2,0x32445247,96);
 g2[36+song*4+track]=m?m->nudge:0;
 if(m&&!others&&!get(row+4)){
  put(row+4,song_frames);put(row,m->speed_q16);
  c[1008+song]=(c[1008+song]&0xf0)|m->song_mode;
  c[976+song*2]=m->chop_div;c[977+song*2]=m->chop_offset;
  unsigned bpm_sum=0;for(unsigned i=0;i<16;i++)bpm_sum=(bpm_sum+get16(g+4+i*2))&65535;
  if(get(g)!=0x31445247||get16(g+36)!=bpm_sum){memset(g,0,38);put(g,0x31445247);}
  put16(g+4+song*2,m->bpm_q8);bpm_sum=0;for(unsigned i=0;i<16;i++)bpm_sum=(bpm_sum+get16(g+4+i*2))&65535;put16(g+36,bpm_sum);
  g2[4+song]=m->grid_offset_encoded;g2[20+song]=m->take_preset;
  extension(g3,0x33445247,80);g3[4+song]=m->grid_beats;put(g3+20+song*4,m->grid_origin);put16(g3+84,sum(g3+4,80));
  extension(g4,0x34445247,16);g4[4+song]=m->beats_per_bar==4?0:m->beats_per_bar;put16(g4+20,sum(g4+4,16));
  put(tr+8+song*4,m->song_head_frames);put(tr+72+song*4,m->song_tail_frames);
 }
 put16(g2+100,sum(g2+4,96));put16(tr+6,sum(tr+8,384));
 return true;
}

bool music_controls_capture(const uint8_t c[3072],unsigned song,unsigned track,take_metadata *m){
 if(!c||!m||song>=16||track>=4||get(c)!=0x53453341||!take_valid(m))return false;
 const uint8_t *row=c+8+song*44,*entry=c+1552+(song*4+track)*16,*tr=c+2576;
 if(row[8+track]!=1)return false;
 m->song_frames=get(row+4);m->speed_q16=get(row);m->pan=entry[14];m->muted=(c[1008+song]>>(4+track))&1;
 m->song_mode=c[1008+song]&15;m->chop_div=c[976+song*2];m->chop_offset=c[977+song*2];
 m->content_flags=entry[13]&7;
 bool trim=get(tr)==0x324d5254&&get16(tr+6)==sum(tr+8,384);
 m->track_head_blocks=trim?get16(tr+136+(song*4+track)*2):0;
 m->track_tail_blocks=trim?get16(tr+264+(song*4+track)*2):0;
 m->song_head_frames=trim?get(tr+8+song*4):0;m->song_tail_frames=trim?get(tr+72+song*4):0;
 const uint8_t *g=c+1024,*g2=g+256,*g3=g+368,*g4=g+456;
 unsigned bpm_sum=0;for(unsigned i=0;i<16;i++)bpm_sum=(bpm_sum+get16(g+4+i*2))&65535;
 m->bpm_q8=get(g)==0x31445247&&get16(g+36)==bpm_sum?get16(g+4+song*2):0;
 bool two=get(g2)==0x32445247&&get16(g2+100)==sum(g2+4,96);
 bool three=two&&get(g3)==0x33445247&&get16(g3+84)==sum(g3+4,80);
 bool four=two&&get(g4)==0x34445247&&get16(g4+20)==sum(g4+4,16);
 m->nudge=two?g2[36+song*4+track]:0;m->grid_offset_encoded=two?g2[4+song]:0;
 unsigned preset=two?g2[20+song]:0;m->take_preset=preset==1||preset==2||preset==4||preset==8?preset:0;
 m->grid_beats=three?g3[4+song]:0;m->grid_origin=three?get(g3+20+song*4):0;
 unsigned bpb=four?g4[4+song]:0;m->beats_per_bar=bpb==3||bpb==5||bpb==7?bpb:0;
 m->beat_frames=m->grid_beats&&m->song_frames?m->song_frames/m->grid_beats:
    m->bpm_q8?(uint32_t)((uint64_t)48000*60*256/m->bpm_q8):0;
 return take_valid(m);
}
