#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/mman.h>
typedef uint64_t UINT64;typedef uint32_t UINT32;typedef uintptr_t UINTN;typedef int BOOLEAN;
#define STATIC static
#define VOID void
#define TRUE 1
#define FALSE 0
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define DEBUG_ERROR 1
static void Debug(unsigned l,const char *f,...){(void)l;(void)f;}
#define DEBUG(a) Debug a
#include "../../mu/Platforms/Xiaomi/degasPkg/Include/GceArena.h"
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/GceFramePacket.h"
static unsigned Maps,Runs,Stops,Cleans,Barriers;static UINT32 MapError,StartError;
static UINT32 GceMappingInstall(UINT32 *root,UINT32 *leaf){
 assert((UINTN)root==DEGAS_GCE_ROOT_PA && (UINTN)leaf==DEGAS_GCE_LEAF_PA);Maps++;return MapError;
}
static void ArmDataSynchronizationBarrier(void){Barriers++;}
static void WriteBackDataCacheRange(void *p,UINTN n){assert((UINTN)p==0xfcc2f000 && n==4992*2712);Cleans++;}
static BOOLEAN GceFrameGuard(void){return TRUE;}
static UINT32 GceRunProgram(const UINT64 *w,UINTN n,UINT32 sentinel,volatile UINT64 *p,BOOLEAN(*guard)(void),BOOLEAN loop,void(*progress)(UINT32,UINT32),BOOLEAN keep){
 assert(Maps==1 && Cleans==1 && Barriers>=2 && n==62 && sentinel==0x76543210);
 assert(loop && keep && !progress && guard==GceFrameGuard);
 assert(w[61]==0x100000011088a200ULL);
 for(UINTN i=0;i<n;i++)assert(w[i]==p[i]);
 Runs++;return StartError;
}
static UINT32 GceStopProgram(void){Stops++;return 0;}
static BOOLEAN GceSmokePoll(UINTN a,UINT32 mask,UINT32 want,UINT32 limit){
 assert(a==0x1400d00c && mask==0x80000000 && !want && limit==100000);return TRUE;
}
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/GceWindows.h"
static void reset(void){
 Maps=Runs=Stops=Cleans=Barriers=MapError=StartError=0;
 mWindowsGceReady=mWindowsGceAttempted=mWindowsGceActive=FALSE;
}
int main(void){
 void *page=mmap((void*)DEGAS_GCE_PACKET_PA,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
 assert(page==(void*)DEGAS_GCE_PACKET_PA);
 reset();assert(GceWindowsStart()==56 && !Runs && !Cleans);
 assert(GceWindowsPrepare()==0 && Maps==1 && !Runs && Barriers==1);
 assert(GceWindowsPrepare()==56 && Maps==1);
 assert(GceWindowsStart()==0 && Runs==1 && mWindowsGceActive);
 assert(GceWindowsStart()==56 && Runs==1);
 GceWindowsStop();assert(Stops==1 && !mWindowsGceActive);
 GceWindowsStop();assert(Stops==1);
 reset();MapError=9;assert(GceWindowsPrepare()==9 && !mWindowsGceReady);
 assert(GceWindowsStart()==56 && !Runs);
 reset();assert(GceWindowsPrepare()==0);((UINT64*)page)[20]^=1;
 assert(GceWindowsStart()==57 && !Runs && !Cleans);
 reset();StartError=46;assert(GceWindowsPrepare()==0 && GceWindowsStart()==46 && !mWindowsGceActive);
 GceWindowsStop();assert(!Stops);
 assert(!munmap(page,4096));
 puts("PASS: prepare without enable, reserved packet publication, mapping failure, corruption, repeat-start rejection, start/stop state; NO HARDWARE");
}
