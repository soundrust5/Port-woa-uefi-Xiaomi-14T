#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef uint32_t UINT32;
typedef uintptr_t UINTN;
typedef int BOOLEAN;
#define VOID void
#define STATIC static
#define TRUE 1
#define FALSE 0
#define DEBUG(x) ((void)0)
static BOOLEAN mUfsActive=TRUE;
static UINT32 mode=0x75, hce=1;
static unsigned writes,delays;
static uintptr_t addresses[8];
static UINT32 values[8];
static UINT32 MmioRead32(uintptr_t a){
 if(a==0x112b0058 || a==0x112b0078)return 0;
 if(a==0x112b0034)return hce;
 assert(a==0x1c00a000);return mode;
}
static void MmioWrite32(uintptr_t a,UINT32 v){
 assert(writes<8);addresses[writes]=a;values[writes++]=v;
 if(a==0x112b0034)hce=v;
 if(a==0x1c00a000)mode=v;
}
static void MicroSecondDelay(unsigned n){delays+=n;}
static void MemoryFence(void){}
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/OsHandoff.h"
int main(void){
 DegasOsHandoff();
 assert(!mUfsActive && mOsHandoffDone);
 assert(addresses[0]==0x112b0024 && values[0]==0);
 assert(addresses[1]==0x112b0060 && values[1]==0);
 assert(addresses[2]==0x112b0080 && values[2]==0);
 assert(addresses[3]==0x112b0034 && values[3]==0);
#if defined(DEGAS_DIAGNOSTIC_EBS_WATCHDOG) && DEGAS_DIAGNOSTIC_EBS_WATCHDOG
 assert(writes==7 && addresses[4]==0x1c00a004 && values[4]==0x5008);
 assert(addresses[5]==0x1c00a000 && values[5]==0x22000035);
 assert(addresses[6]==0x1c00a008 && values[6]==0x1971);
#else
 assert(writes==5 && addresses[4]==0x1c00a000 && values[4]==0x22000074);
#endif
 {unsigned before=writes;assert(!delays);DegasOsHandoff();assert(writes==before);}
 puts("PASS: stop order, watchdog unrelated bits preserved, idempotent handoff");
}
