#ifndef SP1_PREVIEW422_H
#define SP1_PREVIEW422_H
#include <stdint.h>
#include <stdbool.h>
/* Replaces the old aligned16-byte packed-status/generation cache. 17 bits per
 * ordinal cover the maximum entries fitting an8192-sector index. No I/O and
 * no retry loop in the UI reader. One serialized storage-thread writer. */
typedef struct {uint32_t sequence,words[3];} cat422_preview;
_Static_assert(sizeof(cat422_preview)==16,"preview must not grow retained RAM");
bool cat422_preview_publish(cat422_preview*,unsigned song,const uint32_t ordinal[4]);
bool cat422_preview_read(const cat422_preview*,unsigned song,uint32_t ordinal[4]);
void cat422_preview_clear(cat422_preview*);
bool cat430_preview_publish(cat422_preview*,unsigned,const uint32_t[4],unsigned);
unsigned cat430_preview_empty(const cat422_preview*,unsigned);
/*442: same 16-byte RAM cache. Occupied lanes store their ordinal; empty
 * lanes store the saved count. The ordinal API still returns zero for New. */
bool cat442_preview_publish(cat422_preview*,unsigned,const uint32_t[4],unsigned);
bool cat442_preview_read(const cat422_preview*,unsigned,uint32_t[4]);
/*443: four17-bit ordinal/total pairs, song and validity use141 bits. The
 * remaining19 bits form a seqlock; one storage writer, bounded UI readers.
 * Header low13 bits are payload; bit13 marks an update in progress. */
typedef struct {uint32_t header,words[4];} cat443_preview;
_Static_assert(sizeof(cat443_preview)==20,"bounded total/ordinal cache");
bool cat443_preview_publish(cat443_preview*,unsigned,const uint32_t[4],const uint32_t[4]);
bool cat443_preview_read(const cat443_preview*,unsigned,uint32_t[4],bool totals);
unsigned cat443_preview_empty(const cat443_preview*,unsigned);
void cat443_preview_clear(cat443_preview*);
bool cat517_preview_lanes(const cat443_preview*,unsigned,uint32_t[4],uint32_t[4]);
#endif
