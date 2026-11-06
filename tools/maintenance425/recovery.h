#ifndef STORAGE_RECOVERY_H
#define STORAGE_RECOVERY_H
#include <stdbool.h>
#include <string.h>
/* Exact command only. Evaluated before the read-error latch. */
static inline bool storage_boot_command(const char *s,unsigned n){
 return n==9&&!memcmp(s,"S396BOOT!",9);
}
#endif
