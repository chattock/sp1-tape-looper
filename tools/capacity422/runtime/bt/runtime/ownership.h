#ifndef SOURCE_OWNERSHIP_H
#define SOURCE_OWNERSHIP_H
#include <stdbool.h>
#include <stdint.h>
/* Shutdown can assert RESET before UART/RTS have ever been initialized.
 * Accept that exact low-reset/input-RTS state, with UART/CS guards external.
 * Preserve the existing fully initialized, low-reset/low-RTS case. */
static inline bool source_retained_safe(uint32_t reset_cnf,uint32_t rts_cnf,uint32_t reset_out,uint32_t rts_out){
 return reset_cnf==1u&&(rts_cnf==1u||rts_cnf==2u)&&!reset_out&&!rts_out;
}
#endif
