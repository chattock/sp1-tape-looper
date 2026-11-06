#ifndef SP1_AUTO_H
#define SP1_AUTO_H
#include <stdint.h>
int sp1_auto_enabled(void);
unsigned sp1_active_mode(void);
int sp1_claim(unsigned mode);
int sp1_forward_ready(unsigned mode);
void sp1_connection(unsigned connected,unsigned streaming);
void sp1_auto_discovery_stop(unsigned winner);
void sp1_midi_prepare(void);
int sp1_relay_active(void);
int sp1_relay_begin(void);
void sp1_relay_search_done(unsigned reason);
void sp1_source_connection(unsigned connected,unsigned streaming);
int sp1_source_peer(const uint8_t *address);
#endif
