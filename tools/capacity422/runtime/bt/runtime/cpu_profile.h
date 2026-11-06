#ifndef BT_CPU_PROFILE_H
#define BT_CPU_PROFILE_H
#include <stdint.h>
#include <string.h>
#include "audio_decode.h"
/* Audio-thread updates; arm only before active test, report after stop. */
static struct {
 uint32_t last[4], path, step, recording, counts[3];
 uint32_t worst[9]; /* total, phases0..3,step,path */
 uint32_t post_max, full_max, full_over, blocks, click_max, speed_min, speed_max;
} cpu;
static volatile uint32_t cpu_enabled;
/* Reset on the audio thread, never midway through a measured block. */
void bt308_profile_arm(void){cpu_enabled=2;}
void bt341_profile_stop(void){cpu_enabled=0;}
static inline void cpu_begin(unsigned recording){if(cpu_enabled==2){memset(&cpu,0,sizeof cpu);cpu.speed_min=UINT32_MAX;cpu_enabled=1;}if(cpu_enabled){memset(cpu.last,0,sizeof cpu.last);cpu.path=0;cpu.recording=recording;}}
static void __attribute__((noinline)) cpu_phase(unsigned k,unsigned us){if(cpu_enabled&&k<4)cpu.last[k]=us;}
static void __attribute__((noinline)) cpu_path(unsigned step,unsigned path){if(cpu_enabled){cpu.step=step;cpu.path=path;}}
static inline void cpu_mixer_end(unsigned us){
 if(cpu_enabled!=1)return;
 if(cpu.step<cpu.speed_min)cpu.speed_min=cpu.step;if(cpu.step>cpu.speed_max)cpu.speed_max=cpu.step;
 ++cpu.counts[cpu.path<3?cpu.path:0];
 if(us>cpu.worst[0]){cpu.worst[0]=us;for(unsigned k=0;k<4;k++)cpu.worst[k+1]=cpu.last[k];cpu.worst[5]=cpu.step;cpu.worst[6]=cpu.path;cpu.worst[7]=audio_decode_profile.cycles/64u;cpu.worst[8]=audio_decode_profile.frames;}
}
static inline void cpu_submit(unsigned full_us,unsigned post_us,unsigned click_us){
 if(cpu_enabled!=1)return;
 ++cpu.blocks;if(full_us>=5333)++cpu.full_over;
 if(full_us>cpu.full_max)cpu.full_max=full_us;
 if(post_us>cpu.post_max)cpu.post_max=post_us;
 if(click_us>cpu.click_max)cpu.click_max=click_us;
}
#endif
