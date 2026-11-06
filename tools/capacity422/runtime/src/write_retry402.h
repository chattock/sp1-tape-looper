#ifndef SP1_WRITE_RETRY402_H
#define SP1_WRITE_RETRY402_H
/*406 cached control recovery: cleanup outcome, CMD12 probes, CMD13 probes, last R1. */
extern volatile uint32_t write406_trace[4];
#include <stdbool.h>
#include <stdint.h>
/* Cached failure: high byte = stage, middle16 = block index, low byte = token
 * (255 means no token). STATUS uses middle16 for R1 bits31..16 and low8 for
 * R1 bits15..8, with bit7 also marking SWITCH_ERROR. No report bus queries. */
enum { W402_ADMISSION=1,W402_NOT_READY,W402_CMD24,W402_CMD25,W402_TOKEN,
       W402_TIMEOUT,W402_STOP,W402_BUSY,W402_STATUS,W402_BUDGET,W402_SINGLE };
#define W402_REASON(stage,index,detail) (((uint32_t)(stage)<<24)|((uint32_t)(index)<<8)|(uint8_t)(detail))
typedef struct {uint32_t first,last,retries,recovered,exhausted;uint16_t first_ms,recovery_ms;} write_retry402_trace;
typedef struct {
 void *ctx;
 bool (*write)(void *,uint32_t,const void *,uint32_t);
 bool (*retry)(void *,uint32_t,const void *,uint32_t,uint32_t budget_ms);
 uint32_t (*now)(void *);
 uint32_t (*reason)(void *);
} write_retry402_io;
static inline bool write_retry402_retryable(uint32_t reason){
 unsigned stage=reason>>24;
 return stage==W402_CMD25||stage==W402_TOKEN||stage==W402_TIMEOUT||stage==W402_STOP;
}
/* Only private audio uses this adapter. Input and LBAs remain immutable.
 * Early failures retain the40ms/two-attempt policy. A completed missing-token
 * failure after40ms gets ONE recovery opportunity, at most20ms, never beyond
 * 250ms from initial entry. A250ms timeout gets no extension. Every replay still
 * requires independent DAT0/READY/TRANSFER checks in the driver. */
static inline bool write_retry402(const write_retry402_io *io,write_retry402_trace *trace,
                                  uint32_t b,const void *p,uint32_t n){
 uint32_t started=io->now(io->ctx);
 if(io->write(io->ctx,b,p,n))return true;
 trace->first=trace->last=io->reason(io->ctx);
 uint32_t elapsed=io->now(io->ctx)-started;
 trace->first_ms=elapsed>65535u?65535u:elapsed;trace->recovery_ms=0;
 if(elapsed>=40u&&elapsed<250u&&(trace->first>>24)==W402_TOKEN&&(trace->first&255u)==255u){
  uint32_t budget=250u-elapsed;if(budget>20u)budget=20u;
  trace->retries++;
  bool ok=io->retry(io->ctx,b,p,n,budget);
  trace->recovery_ms=(io->now(io->ctx)-started)-elapsed;
  if(ok){trace->recovered++;return true;}
  trace->last=io->reason(io->ctx);trace->exhausted++;return false;
 }
 for(unsigned attempt=0;attempt<2&&write_retry402_retryable(trace->last);attempt++){
  elapsed=io->now(io->ctx)-started;
  if(elapsed>=40u)break;
  trace->retries++;
  bool ok=io->retry(io->ctx,b,p,n,40u-elapsed);
  trace->recovery_ms=(io->now(io->ctx)-started)-trace->first_ms;
  if(ok){trace->recovered++;return true;}
  trace->last=io->reason(io->ctx);
 }
 trace->exhausted++;return false;
}
#endif
