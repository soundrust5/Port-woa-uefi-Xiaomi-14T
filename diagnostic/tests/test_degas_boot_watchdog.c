#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define STATIC static
#define VOID void
#define TRUE 1
#define DEBUG(x) ((void)0)
typedef uint32_t UINT32;typedef uint64_t UINT64;typedef int BOOLEAN;
static UINT32 mode=0x7d,length,feeds;static UINT64 now;
static UINT64 GetPerformanceCounter(void){return now;}
static UINT64 GetTimeInNanoSecond(UINT64 n){return n;}
static UINT32 MmioRead32(UINT32 a){return a==0x1c00a000?mode:length;}
static void MmioWrite32(UINT32 a,UINT32 v){if(a==0x1c00a000)mode=v;else if(a==0x1c00a004)length=v;else{assert(a==0x1c00a008 && v==0x1971);feeds++;}}
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/BootWatchdog.h"
int main(void){BootWatchdogArmTenSeconds();assert(length==0x5008 && mode==0x22000035 && feeds==1);
mBootBudgetActive=TRUE;mBootBudgetStart=0;now=59999999999ULL;BootWatchdogFeed();assert(feeds==2);
now=60000000000ULL;BootWatchdogFeed();assert(feeds==2);
now=60000000001ULL;BootWatchdogFeed();assert(feeds==2);
BootWatchdogArmTenSeconds();assert(feeds==3);puts("WDT 10-second encoding and 60-second feed cutoff PASS");}
