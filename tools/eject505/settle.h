#ifndef EJECT505_SETTLE_H
#define EJECT505_SETTLE_H
#include <stdbool.h>
/* Retain the existing ACK window for every setup, diagnostic, recovery and
 * boot path. Shorten only completed autonomous Finder-eject return, with no
 * host owner/upload/serial listener or pending/failed transmission. */
static inline unsigned eject505_settle_ms(bool complete,bool normal,bool ejected,bool host,bool upload,bool listening,bool pending,bool tx_failed){return complete&&normal&&ejected&&!host&&!upload&&!listening&&!pending&&!tx_failed?100u:750u;}
#endif
