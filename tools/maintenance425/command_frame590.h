#ifndef COMMAND_FRAME590_H
#define COMMAND_FRAME590_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
/* Main-thread command scratch only. IRQ RX owns its separate ring; IRQ TX
 * borrows this array until send_bytes completes or cancels under tx_lock.
 * Never alias the upload payload: write readback must remain independent. */
#define COMMAND590_PREFIX 256u
#define COMMAND590_PAYLOAD 8192u
#define COMMAND590_BYTES (COMMAND590_PREFIX+COMMAND590_PAYLOAD)
static size_t command590_frame(uint8_t response[COMMAND590_BYTES],bool library,
 uint32_t seq,uint32_t lba,uint32_t blocks,uint32_t crc){
 if(!blocks||blocks>16)return 0;
 int h=snprintf((char*)response,COMMAND590_PREFIX,"%s DATA seq=%08x lba=%u blocks=%u crc=%08x\n",
  library?"LIBRARY462":"STORAGE396",seq,lba,blocks,crc);
 const char*end=library?"\nLIBRARY462 DONE\n":"\nSTORAGE396 DONE\n";
 size_t tail=strlen(end),payload=(size_t)blocks*512;
 if(h<=0||(unsigned)h>=COMMAND590_PREFIX||(size_t)h+payload+tail>COMMAND590_BYTES)return 0;
 memmove(response+h,response+COMMAND590_PREFIX,payload);
 memcpy(response+h+payload,end,tail);return (size_t)h+payload+tail;
}
#endif
