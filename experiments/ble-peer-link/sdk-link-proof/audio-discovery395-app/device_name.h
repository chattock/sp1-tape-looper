#pragma once
#include <stdint.h>
#define SP1_BT_NAME "SP-1 Tape Looper"
/* AD/EIR length includes the type byte, excludes the length byte itself. */
static const struct {
 uint8_t length,type;
 char name[sizeof(SP1_BT_NAME)-1];
 uint8_t services[4];
} sp1_inquiry={sizeof(SP1_BT_NAME),9,SP1_BT_NAME,{3,3,0x0b,0x11}};
_Static_assert(sizeof(sp1_inquiry)==sizeof(SP1_BT_NAME)+5,"Unexpected EIR padding");
_Static_assert(sizeof(SP1_BT_NAME)+1<=31,"Complete name fits scan response");
