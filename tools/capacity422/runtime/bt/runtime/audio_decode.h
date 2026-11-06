#ifndef SP1_AUDIO_DECODE_H
#define SP1_AUDIO_DECODE_H
#include <stdint.h>
#include <stdbool.h>
/* One ISR producer and one audio-thread consumer. Reset only before RX starts. */
void audio_decode_reset(unsigned prebuffer);
bool audio_decode_packet(unsigned bytes);
void audio_decode_byte(uint8_t b);
unsigned audio_decode_buffered(void);
unsigned audio_decode_rate(void);
unsigned audio_decode_render(int16_t *out,unsigned frames,unsigned gain_q8);
struct audio_decode_stats { uint32_t frames,errors,skipped,overflow,underrun,output,peak_queue,primings,initial_wait,rebuffer_wait,reject_peak,gap_open,gap_count,gap_samples,gap_max; };
extern struct audio_decode_stats audio_decode_stats;
#endif

void audio_decode_bytes(const uint8_t *data, unsigned bytes);

/* Per render block; cycles include interrupt preemption. */
struct audio_decode_profile { uint32_t cycles,frames,max_frame_cycles,calls; };
extern struct audio_decode_profile audio_decode_profile;

unsigned audio_decode_handoff(int16_t *,unsigned,unsigned,bool *);
void audio_decode_discard(void);

unsigned audio_decode_mix(int16_t *,unsigned,unsigned,bool *,bool);
