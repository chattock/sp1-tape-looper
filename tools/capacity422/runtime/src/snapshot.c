#include "snapshot.h"
#include "geometry.h"
static uint32_t u32(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static unsigned u16(const uint8_t *p){return p[0]|(unsigned)p[1]<<8;}
bool music_native_snapshot_extended(const uint8_t c[3072]){
 if(!c)return false;
 const uint8_t *x=c+1536;unsigned sum=0;
 for(unsigned i=16;i<1040;i++)sum=(sum+x[i])&65535u;
 return u32(x)==0x53453358u&&u16(x+4)==1&&u16(x+6)==sum;
}
bool music_native_song_validate(const uint8_t c[3072],const take_metadata tracks[4],unsigned song){
 if(!c||!tracks||song>=16)return false;
 return music_native_song_validate_prechecked(c,tracks,song,music_native_snapshot_extended(c));
}
bool music_native_song_validate_prechecked(const uint8_t c[3072],const take_metadata tracks[4],unsigned song,bool extended){
 return music_native_song_issue(c,tracks,song,extended)==0;
}
unsigned music_native_song_issue(const uint8_t c[3072],const take_metadata tracks[4],unsigned song,bool extended){
 if(!c||!tracks||song>=16||u32(c)!=0x53453341u||u32(c+4)>=16)return 2u<<2;
 const uint8_t *row=c+8+song*44,*x=c+1536;
 if(u32(row)>131072)return 3u<<2;
 for(unsigned i=0;i<4;i++){
  const take_metadata *m=&tracks[i];unsigned present=row[8+i];
  if(present>1||((m->audio_blocks!=0)!=(present!=0)))return (4u<<2)|i;
  if(!present)continue;
  if(!extended)return (5u<<2)|i;
  if(!take_valid(m))return (6u<<2)|i;
  const uint8_t *entry=x+16+(song*4+i)*16;unsigned codec=entry[12];
  if(codec!=4&&codec!=5&&codec!=6)return (7u<<2)|i;
  unsigned fpb=codec==6?496:280;
  if(m->frames_per_block!=fpb||m->format!=(codec==6?2u:1u))return (8u<<2)|i;
  uint32_t frames=u32(row+4),blocks=u32(row+12+i*4);
  if(!blocks){uint64_t rounded=((uint64_t)frames+fpb/2)/fpb;if(rounded>UINT32_MAX)return (9u<<2)|i;blocks=(uint32_t)rounded;}
  music_geometry g;
  if(!music_geometry_load(fpb,frames,blocks,u32(row+28+i*4),u32(entry+4),u32(entry),true,m->audio_blocks,&g))return (9u<<2)|i;
  if(g.frames!=m->frames||g.start_frames!=m->start_frames){
   /*418: pre417 migration decoded nearest-block anchors with a floor-only
    * exact-anchor gate. Admit only that precisely reproducible old result,
    * never arbitrary differences or a different extent/codec/checksum. */
   uint32_t anchor=u32(entry),start=u32(row+28+i*4);music_geometry old;
   bool legacy=anchor/fpb!=start&&((uint64_t)anchor+fpb/2)/fpb==start&&
    music_geometry_load(fpb,frames,blocks,start,0,0,false,m->audio_blocks,&old)&&
    old.frames==m->frames&&old.start_frames==m->start_frames;
   if(!legacy)return ((g.frames!=m->frames?10u:11u)<<2)|i;
  }
  if(m->loop_blocks&&blocks!=m->loop_blocks)return (12u<<2)|i;
  if(u32(c+716+(song*4+i)*4)!=m->content_blocks)return (13u<<2)|i;
 }
 return 0;
}

/* Normalize only proven historical geometry in the private controls snapshot.
 * The immutable take header remains authoritative: no audio/header writes.
 * Caller publishes via the existing atomic catalog transaction, if saving. */
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=v>>(i*8);}
bool music_native_song_normalize(uint8_t c[3072],const take_metadata tracks[4],unsigned song,bool extended){
 if(music_native_song_issue(c,tracks,song,extended))return false;
 uint8_t *row=c+8+song*44;bool changed=false;
 for(unsigned i=0;i<4;i++){
  const take_metadata *m=&tracks[i];if(!row[8+i])continue;
  uint8_t *entry=c+1552+(song*4+i)*16;music_geometry g;
  uint32_t blocks=u32(row+12+i*4);if(!blocks)blocks=(uint32_t)(((uint64_t)u32(row+4)+m->frames_per_block/2)/m->frames_per_block);
  if(!music_geometry_load(m->frames_per_block,u32(row+4),blocks,u32(row+28+i*4),u32(entry+4),u32(entry),true,m->audio_blocks,&g))return false;
  if(g.frames==m->frames&&g.start_frames==m->start_frames)continue;
  put32(row+28+i*4,m->start_frames/m->frames_per_block);
  put32(entry,m->start_frames);put32(entry+4,m->frames);changed=true;
 }
 if(changed){unsigned sum=0;for(unsigned i=1552;i<2576;i++)sum=(sum+c[i])&65535u;c[1542]=sum;c[1543]=sum>>8;}
 return true;
}
