#ifndef STORAGE_PROTOCOL_H
#define STORAGE_PROTOCOL_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
static bool hex8(const char *p,uint32_t *out){
 uint32_t n=0;for(unsigned i=0;i<8;i++){
  unsigned v=p[i]>='0'&&p[i]<='9'?p[i]-'0':p[i]>='a'&&p[i]<='f'?p[i]-'a'+10:16;
  if(v>15)return false;n=(n<<4)|v;
 }*out=n;return true;
}
static bool read_request(const char *line,unsigned length,uint32_t capacity,uint32_t *seq,uint32_t *lba,uint32_t *n){
 return length==31&&!memcmp(line,"R396 ",5)&&line[13]==' '&&line[22]==' '&&
 hex8(line+5,seq)&&hex8(line+14,lba)&&hex8(line+23,n)&&capacity&&*n&&*n<=16&&*lba<capacity&&*n<=capacity-*lba;
}
#endif
