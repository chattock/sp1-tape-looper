#ifndef SP1_FRESH529_H
#define SP1_FRESH529_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../capacity422/paged/store.h"
#define FRESH529_ADDRESS 0x7c000u
#define FRESH529_MAGIC 0x39524653u
#define FRESH529_CAPACITY 7733248u
#define FRESH529_INDEX 8192u
#define FRESH529_WORK 524288u
#define FRESH529_RESERVE 262144u
/* A complete request is supplied ONLY in the explicitly destructive BIN.
 * Completion is a one-way flash write. Any incomplete completion retries
 * before runtime; normal BINs contain an erased request page. */
typedef struct {uint32_t magic,version,capacity,seal,complete;} fresh529_request;
static inline bool fresh529_authorized(const fresh529_request*r){return r&&r->magic==FRESH529_MAGIC&&r->version==1&&r->capacity==FRESH529_CAPACITY&&r->seal==~(r->magic^r->version^r->capacity);}
static inline bool fresh529_erased(const fresh529_request*r){return r&&r->magic==UINT32_MAX&&r->version==UINT32_MAX&&r->capacity==UINT32_MAX&&r->seal==UINT32_MAX&&r->complete==UINT32_MAX;}
static inline bool fresh529_pending(const fresh529_request*r){return fresh529_authorized(r)&&r->complete!=0;}
typedef struct {uint32_t capacity,fat,reserved,data,clusters,end,audio_first,audio_blocks,index[2],work_first;} fresh529_geometry;
typedef struct {void*ctx;bool(*read)(void*,uint32_t,void*,uint32_t);bool(*write)(void*,uint32_t,const void*,uint32_t);bool(*sync)(void*);void(*progress)(void*,unsigned,uint32_t,uint32_t);} fresh529_io;
typedef struct {pg422 index;fresh529_io io;fresh529_geometry g;uint32_t index_first;uint8_t sectors[8192],check[8192];} fresh529_work;
bool fresh529_geometry_make(uint32_t,fresh529_geometry*);
bool fresh529_initialize(fresh529_io,uint32_t,fresh529_work*);
#endif
