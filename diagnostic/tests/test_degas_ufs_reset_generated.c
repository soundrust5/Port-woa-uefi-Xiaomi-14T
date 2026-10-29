#include <stdint.h>
#include <assert.h>
#define STATIC static
#define VOID void
#define BOOLEAN int
#define UINT64 uint64_t
#define DEBUG(x) ((void)0)
typedef struct {uint64_t Arg0,Arg1,Arg2,Arg3,Arg4,Arg5,Arg6,Arg7;} ARM_SMC_ARGS;
static unsigned calls,delay,scenario;
static void ArmCallSmc(ARM_SMC_ARGS *a){assert(a->Arg0==0xc2000276&&a->Arg1==2&&a->Arg2==calls&&a->Arg3==0);a->Arg0=(scenario==1&&calls==0)||(scenario==2&&calls==1)?(uint64_t)-1:0;calls++;}
static void MicroSecondDelay(unsigned n){assert(n==(calls==1?10:10000));delay+=n;}
STATIC BOOLEAN UfsDeviceReset(VOID){
 ARM_SMC_ARGS Low={0},High={0};
 Low.Arg0=0xc2000276;Low.Arg1=2;Low.Arg2=0;
 DEBUG((DEBUG_ERROR,"DEGAS UFS DEVICE RESET asserting\n"));
 ArmCallSmc(&Low);
 DEBUG((DEBUG_ERROR,"DEGAS UFS DEVICE RESET low a0=%Lx a1=%Lx\n",(UINT64)Low.Arg0,(UINT64)Low.Arg1));
 // HCE was verified zero by the caller, before any reset operation.
 MicroSecondDelay(10);
 High.Arg0=0xc2000276;High.Arg1=2;High.Arg2=1;
 // Always release, including when asserting returned an error.
 ArmCallSmc(&High);
 DEBUG((DEBUG_ERROR,"DEGAS UFS DEVICE RESET high a0=%Lx a1=%Lx\n",(UINT64)High.Arg0,(UINT64)High.Arg1));
 MicroSecondDelay(10000);
 return Low.Arg0==0 && High.Arg0==0;
}

int main(void){assert(UfsDeviceReset());assert(calls==2&&delay==10010);calls=delay=0;scenario=1;assert(!UfsDeviceReset());assert(calls==2&&delay==10010);calls=delay=0;scenario=2;assert(!UfsDeviceReset());assert(calls==2&&delay==10010);return 0;}
