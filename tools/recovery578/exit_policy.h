#ifndef SP1_EXIT578_H
#define SP1_EXIT578_H
#include <stdbool.h>
/* Quiescence is necessary for either destination. Recovery authority may
 * remain for bootloader, never for ordinary return to musical runtime. */
static inline bool exit578_admit(bool boot,bool host,bool upload,bool busy,bool gate,bool failed,bool host_failed){
 if(host||upload||busy)return false;
 return boot||(!gate&&!failed&&!host_failed);
}
#endif
