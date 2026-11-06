/* Bounded audio-only discovery. No device IO; shared by radio and host tests. */
#ifndef SP1_AUDIO_DISCOVERY_POLICY_H
#define SP1_AUDIO_DISCOVERY_POLICY_H
#include <stdint.h>
#include <string.h>
#define AD_MAX 8
struct ad_entry { uint8_t address[6], compatible; };
struct ad_table { struct ad_entry entries[AD_MAX]; unsigned count,overflow,uncertain; };
static int ad_add(struct ad_table *t,const uint8_t *a){
 unsigned any=0,all=255;for(unsigned j=0;j<6;j++){any|=a[j];all&=a[j];}
 if(!any||all==255)return -1;
 for(unsigned i=0;i<t->count;i++)if(!memcmp(t->entries[i].address,a,6))return (int)i;
 if(t->count==AD_MAX){t->overflow=1;return -1;}
 unsigned i=t->count++;memcpy(t->entries[i].address,a,6);return (int)i;
}
/* An incomplete search must never pretend there is only one eligible target. */
static int ad_unique(const struct ad_table *t){
 if(t->overflow||t->uncertain)return -1;
 int found=-1;for(unsigned i=0;i<t->count;i++)if(t->entries[i].compatible){if(found>=0)return -1;found=(int)i;}
 return found;
}
#endif
