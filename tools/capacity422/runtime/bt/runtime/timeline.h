#pragma once
#include <stdint.h>
/* First events retained; overflow is explicit, never silently overwritten.
 * Caller serializes access across UART ISR and audio thread. */
#define BT_TRACE_CAPACITY 24
struct bt_trace_event { uint32_t ms, kind, value; };
struct bt_trace { struct bt_trace_event events[BT_TRACE_CAPACITY]; uint32_t count, dropped; };
static inline void bt_trace_add(struct bt_trace *t,uint32_t ms,uint32_t kind,uint32_t value){
 if(t->count==BT_TRACE_CAPACITY){if(t->dropped!=UINT32_MAX)++t->dropped;return;}
 t->events[t->count++]=(struct bt_trace_event){ms,kind,value};
}
