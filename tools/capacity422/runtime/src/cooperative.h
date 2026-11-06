#ifndef SP1_COOPERATIVE_IO_H
#define SP1_COOPERATIVE_IO_H
#include <stdbool.h>
#include <stdint.h>
/* Control-thread adapter only. Callback must use disjoint scratch and must not
 * mutate the catalog or issue a second transaction. Reads/writes never retry.
 * Flush returns1 durable,0 confirmed safe suspension,-1 failure. */
typedef struct {
 void *ctx;
 bool (*service)(void *);
 bool (*read)(void *,uint32_t,void *,uint32_t);
 bool (*write)(void *,uint32_t,const void *,uint32_t);
 int (*flush)(void *);
} cooperative_io;
bool cooperative_read(const cooperative_io *,uint32_t,void *,uint32_t);
bool cooperative_write(const cooperative_io *,uint32_t,const void *,uint32_t);
bool cooperative_flush(const cooperative_io *);
#endif
