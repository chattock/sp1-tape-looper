#pragma once
#include <stdbool.h>
#include <stdint.h>
void sp1_bt_probe_byte(uint8_t b);
void sp1_bt_probe_service(bool allowed);
void sp1_bt_probe_report(void);
void sp1_bt_probe_stop(void);

bool sp1_setup_return_safe(void);
bool sp1_setup_recovery_required(void);
void sp1_setup_write_begin(void);
void sp1_setup_write_verified(void);
