#ifndef SETUP_STARTUP_H
#define SETUP_STARTUP_H
#include <stdbool.h>
#include <stdint.h>
struct setup_power {uint32_t pressed_at,absent_at;bool armed,held,exit_on_release,absent;};
static inline bool setup_should_off(struct setup_power*s,uint32_t now,bool pressed,bool vbus,bool pgood){
 if(vbus||pgood)s->absent=false;
 else if(!s->absent){s->absent=true;s->absent_at=now;}
 else if((uint32_t)(now-s->absent_at)>=1500)return true;
 if(!pressed){s->armed=true;s->held=false;if(s->exit_on_release)return true;}
 else if(s->armed){
  if(!s->held){s->held=true;s->pressed_at=now;}
  if((uint32_t)(now-s->pressed_at)>=2000)s->exit_on_release=true;
 }
 return false;
}
struct setup_host {uint32_t since;bool waiting,ready;};
static inline bool setup_hello(struct setup_host*s,uint32_t now,bool dtr){
 if(!dtr){s->waiting=false;s->ready=false;return false;}
 if(s->ready)return false;
 if(!s->waiting){s->waiting=true;s->since=now;}
 if((uint32_t)(now-s->since)<250)return false;
 s->ready=true;return true;
}
#endif
