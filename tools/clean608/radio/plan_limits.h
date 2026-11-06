#pragma once
#include <stdint.h>
#include <stdbool.h>
#define MAX_PLAN_SECTORS 29u
static uint8_t plan_pages[MAX_PLAN_SECTORS];
static uint32_t plan_address(unsigned i){return (uint32_t)plan_pages[i]*4096u;}
static unsigned plan_count,plan_received;
static bool allowed_address(uint32_t a){return a==0x1000||(a>=0x3000&&a<0x1e000&&a%4096==0);}
static bool migration_allowed(uint32_t a){for(unsigned i=0;i<plan_count;i++)if(plan_address(i)==a)return true;return false;}
