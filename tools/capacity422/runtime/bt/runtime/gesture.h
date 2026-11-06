/* Pure eligibility-gated triple-FN recognizer. No device IO or flash actions. */
#ifndef SP1_PEER_GESTURE_H
#define SP1_PEER_GESTURE_H
#include <stdint.h>
#include <stdbool.h>
struct peer_gesture { int64_t down_at,last_up,menu_at,track_at; unsigned taps,mode,track; bool prev_fn,release,submenu; };
/* Return 1 consumed, >=2 is radio mode + 2 (0=off), 0 ordinary looper. */
static unsigned peer_gesture_step(struct peer_gesture *g,int64_t now,bool allowed,bool fn,unsigned track){
 if(!allowed){*g=(struct peer_gesture){.prev_fn=fn};return 0;}
 bool rise=fn&&!g->prev_fn,fall=!fn&&g->prev_fn;g->prev_fn=fn;
 if(g->mode==3){
  if(fn&&!g->release&&now-g->down_at>=3000){g->release=true;return 2;} /* Explicit OFF. */
  if(!fn&&!track)*g=(struct peer_gesture){0};return 1;
 }
 if(g->mode==2){
  if(now-g->menu_at>=15000||(rise&&!g->release)){g->mode=3;return 1;}
  if(g->release){if(!fn&&!track)g->release=false;return 1;}
  if(track!=g->track){g->track=track;g->track_at=now;}
  if((track>=1&&track<=4)&&now-g->track_at>=100){
   if(!g->submenu&&track==1){g->submenu=true;g->release=true;g->track=0;return 1;}
   if(g->submenu&&track==4){g->submenu=false;g->release=true;g->track=0;return 1;}
   unsigned choice=g->submenu?(track==1?2:track==2?3:0):(track==2?1:track==3?5:4);
   g->mode=3;return choice+2;
  }
  return 1;
 }
 if(g->mode==1){
  if(fall){g->mode=0;g->taps=2;g->last_up=now;return 1;}
  if(fn&&now-g->down_at>=1000){g->mode=3;return 8;}
  return 1;
 }
 if(!fn&&now-g->last_up>350)g->taps=0;
 if(rise){
  if(now-g->last_up>350)g->taps=0;
  g->down_at=now;
  if(g->taps>=2&&!track){g->mode=1;return 1;}
 }
 if(track)g->taps=0;
 if(fall){if(now-g->down_at>=25&&now-g->down_at<300)++g->taps;else g->taps=0;g->last_up=now;}
 return 0;
}
/* Single FN dismisses only an already-visible status view. The third hold's
 * release belongs to mode3, so opening cannot immediately close itself.
 * Seed one consumed tap: a subsequent rapid second/third press can still
 * form the established explicit3-second Bluetooth-OFF gesture. */
static unsigned peer_status_exit_step(struct peer_gesture *g,int64_t now,bool allowed,bool fn,unsigned track,bool visible){
 if(g->mode==4){
  if(fn)return 1;
  bool tap=now-g->down_at<500;
  *g=(struct peer_gesture){.taps=tap?1:0,.last_up=now};
  return tap?9:1; /* action9 -> close view, never disconnect */
 }
 if(allowed&&visible&&!g->mode&&fn&&!g->prev_fn&&!track){g->mode=4;g->down_at=now;g->prev_fn=true;return 1;}
 return 0;
}
#endif
