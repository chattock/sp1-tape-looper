#include "storage373.h"
/* Storage-thread admission before publishing readiness; no audio-path work. */
static bool writable;
bool storage373_legacy(const uint8_t *p, size_t n)
{
 /* Match the maintained SE3A format, never infer blank media is disposable.
  * This is format admission, not an integrity claim for every track. */
 return p && n == 1024 && p[0]==0x41 && p[1]==0x33 && p[2]==0x45 && p[3]==0x53
     && p[4]<16 && p[5]==0 && p[6]==0 && p[7]==0;
}
void storage373_admit(bool recognized) { writable = recognized; }
bool storage373_writable(void) { return writable; }
