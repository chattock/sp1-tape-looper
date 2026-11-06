/* A scheduling hint only. Every byte is still parsed by the unchanged cores.
 * Missing a hint cannot block progress: main retains its1ms timed sleep. */
#include <stdbool.h>
static bool wake545(unsigned goal,unsigned before,unsigned after,bool new_fault){
 return new_fault||(goal>0&&goal<=256&&before<goal&&after>=goal);
}
