#ifndef BREATH431_H
#define BREATH431_H
#include <stdint.h>
/*435: 3.2 seconds fully dark, 0.8s fade up, 0.8s fade down.
 * Smoothstep lookup in flash; integer interpolation only at LED cadence. */
static unsigned breath431_level(uint32_t ms){
 static const uint8_t ease[17]={0,2,8,18,30,44,61,78,96,114,131,148,162,174,184,190,192};
 unsigned p=ms%4800u;if(p<3200u)return 0;
 p-=3200u;if(p>800u)p=1600u-p;
 unsigned i=p/50u,f=p%50u;if(i>=16)return 192;
 return ease[i]+(ease[i+1]-ease[i])*f/50u;
}
#endif
