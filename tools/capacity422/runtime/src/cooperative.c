#include "cooperative.h"
#include <stddef.h>
static bool bounds(uint32_t b,uint32_t n){return n&&n<=UINT32_MAX/512&&b<=UINT32_MAX-(n-1);}
bool cooperative_read(const cooperative_io *io,uint32_t b,void *p,uint32_t n){
 if(!io||!p||!io->service||!io->read||!bounds(b,n))return false;
 uint8_t *out=p;
 while(n){uint32_t k=n>2?2:n;if(!io->service(io->ctx)||!io->read(io->ctx,b,out,k))return false;b+=k;out+=k*512;n-=k;}
 return true;
}
bool cooperative_write(const cooperative_io *io,uint32_t b,const void *p,uint32_t n){
 if(!io||!p||!io->service||!io->write||!bounds(b,n))return false;
 const uint8_t *in=p;
 while(n){uint32_t k=n>2?2:n;if(!io->service(io->ctx)||!io->write(io->ctx,b,in,k))return false;b+=k;in+=k*512;n-=k;}
 return true;
}
bool cooperative_flush(const cooperative_io *io){
 if(!io||!io->service||!io->flush)return false;
 for(unsigned i=0;i<64;i++){
  if(!io->service(io->ctx))return false;
  int result=io->flush(io->ctx);if(result==1)return true;if(result!=0)return false;
 }
 return false; /* bounded attempts; caller poisons the unpublished transaction */
}
