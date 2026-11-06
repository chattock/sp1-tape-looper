#ifndef SP1_WORKSPACE422_H
#define SP1_WORKSPACE422_H
#include "io.h"
#include <stddef.h>
#include "../../commit421/runtime/src/take.h"
/* Alternative member of the existing16 KiB storage workspace. No retained
 * allocation, no audio buffer changes, no pointers survive returning to DSP. */
typedef struct {
 union {
  pool_store legacy_layout;
  struct {pg422 index,checkpoint;cat422_device device;uint8_t sector[512];};
 };
 take_metadata loaded[4];
 uint8_t controls[3072];
} cat422_workspace;
_Static_assert(sizeof(cat422_workspace)<=16384,"paged catalog must fit shared storage workspace");
typedef struct {pool_store pool;take_metadata loaded[4];uint8_t controls[3072];} cat422_legacy_output_layout;
_Static_assert(offsetof(cat422_workspace,loaded)==offsetof(cat422_legacy_output_layout,loaded),"active metadata output ABI");
_Static_assert(offsetof(cat422_workspace,controls)==offsetof(cat422_legacy_output_layout,controls),"control output ABI");
#endif
