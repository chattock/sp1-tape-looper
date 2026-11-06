#pragma once
#include <stdbool.h>
/* Retain only the existing companion link while its own app owns storage.
 * This does not admit a new mode, USB transfer or concurrent USB audio. */
static inline bool companion_storage_allowed(unsigned mode,unsigned phase,bool active,
 bool own_storage){
 return mode==4 && phase==4 && active && own_storage;
}
