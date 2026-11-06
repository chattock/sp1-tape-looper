#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef CONFIG_SP1_BT_ID_PROBE
void sp1_bt_probe_byte(uint8_t b);
void sp1_bt_probe_fault_byte(uint8_t b);
void sp1_bt_probe_fault_enter(void);
void sp1_bt_peer_request(unsigned role);
unsigned sp1_bt_peer_lights(void);
void sp1_bt_probe_service(bool allowed);
void sp1_bt_probe_report(void);
void sp1_bt_probe_stop(void);
#else
static inline void sp1_bt_probe_byte(uint8_t b) { (void)b; }
static inline void sp1_bt_probe_fault_byte(uint8_t b) { (void)b; }
static inline void sp1_bt_probe_fault_enter(void) {}
static inline void sp1_bt_probe_service(bool allowed) { (void)allowed; }
static inline void sp1_bt_probe_report(void) {}
static inline void sp1_bt_probe_stop(void) {}
#endif
