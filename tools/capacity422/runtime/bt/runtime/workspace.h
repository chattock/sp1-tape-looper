#ifndef BT239_WORKSPACE_H
#define BT239_WORKSPACE_H
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "state_client.h"
/* Exclusive modes: no capacity reduction to the tested 3200-byte SBC ring.
 * Audio and RX must be quiescent before changing the active union member. */
struct bt_nonce { uint32_t words[8],word,saved_config; unsigned bytes,count; int status; int64_t at; };
struct bt_peer {
 state_client client;
 uint8_t fifo[8][40],length[8],incoming[40];
 uint32_t write,read,invalid,received,sent,generation,disconnects;
 uint8_t radio_state,live,proven;
 _Alignas(4) uint8_t snapshot_scratch[RICH_BYTES];
};
union bt_workspace { uint8_t audio[3200];struct bt_nonce rng;struct bt_peer peer; };
_Static_assert(sizeof(union bt_workspace)==3200,"Exclusive workspace grew");
extern union bt_workspace bt_workspace;
#endif
