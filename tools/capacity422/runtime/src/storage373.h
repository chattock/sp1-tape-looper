#ifndef STORAGE373_H
#define STORAGE373_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
bool storage373_legacy(const uint8_t *, size_t);
void storage373_admit(bool);
bool storage373_writable(void);
#include "backend.h"
bool storage373_pool(void);
bool storage523_legacy(void);
void storage523_report(void);
bool storage400_fault(void);
void storage400_report(void);
void storage400_failed(void);
bool storage373_read_controls(music_runtime_workspace *,uint32_t,uint8_t *,uint32_t);
bool storage373_song(unsigned,uint32_t,uint32_t,music_runtime_workspace *);
bool storage373_save(music_runtime_workspace *);
bool storage373_prepare(music_runtime_workspace *,unsigned,uint32_t);
/* Select the path only when acquiring a writer. Repeated calls retain the
 * existing matching writer; fresh_replace never changes an in-flight mode. */
bool storage401_prepare(music_runtime_workspace *,unsigned,uint32_t,bool fresh_replace);
bool storage373_prepare_step(music_runtime_workspace *);
bool storage373_record_ready(unsigned);
bool storage373_writer(void);
bool storage373_abort(void);
uint32_t storage373_written(void);
bool storage373_write(unsigned,uint32_t,const uint8_t *,uint32_t,uint8_t scratch[512]);
bool storage373_finish(music_runtime_workspace *,const take_metadata *);

bool storage373_action(music_runtime_workspace *,unsigned,unsigned,unsigned);
void storage373_scheduling(bool);
bool music_backend_paged(void);
void storage427_warm(music_runtime_workspace*,uint32_t);
bool storage427_audition(unsigned,const take_metadata*);
#endif
