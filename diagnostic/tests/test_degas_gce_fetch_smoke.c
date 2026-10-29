#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint64_t UINT64;typedef uint32_t UINT32;typedef uintptr_t UINTN;
typedef int BOOLEAN;
#define STATIC static
#define CONST const
#define VOID void
#define TRUE 1
#define FALSE 0
#define DEBUG_ERROR 1
static void TestDebug(unsigned l,const char *f,...){(void)l;(void)f;}
#define DEBUG(a) TestDebug a
#include "../../mu/Platforms/Xiaomi/degasPkg/Include/GceArena.h"
static UINT32 R[32],Ticks,Fail,Starts,Resets,Writes,Barriers,Snapshot,LoopMode,ProgressCalls;
static UINT64 Packet[512];static int Ran,Observed;
static void MemoryFence(void){}
static void ArmDataSynchronizationBarrier(void){Barriers++;}
static UINT32 GceSnapshot(void){return Snapshot;}
static void MicroSecondDelay(UINT32 n){assert(n==1 || (LoopMode && n==100));Ticks+=n;}
static UINT32 MmioRead32(UINTN a){
 if(a==0x1400d00c || a==0x1e980080 || a==0x1e980084 || a==0x1e9800b8 || a==0x1e9800bc)return 0;
 if(a==0x1e980054)return 0;
 assert(a>=0x1e980100 && a<=0x1e98017c && !(a&3));
 unsigned off=(a-0x1e980100)/4;
 if(a==0x1e980110 && Ran && LoopMode){
  R[8]=0x1088a200+(Ticks/100%3);R[24]=0x1400; // SPR sentinel need not persist
  if(Fail!=12)R[10]=((Fail==13 && Ticks>800000)?800000:Ticks)/3000;
  if(Fail==14)R[8]=R[9];
  if(Fail==15)R[1]=0;
  if(Fail==16)R[4]=0x10;
 }
 if(a==0x1e980110 && Ran && !Observed && !LoopMode){
  Observed=1;
  if(Fail==2)R[4]=0x10;
   else if(Fail!=1){R[8]=R[9];R[24]=0x12345678;R[10]++;if(Fail==9)R[1]=0;if(Fail==10)R[4]=1;}
 }
 if(Fail==6 && a==0x1e980124)return R[off]^1;
 return R[off];
}
static void MmioWrite32(UINTN a,UINT32 v){
 Writes++;
 if(a==0x1e980030){assert(v==0x3200);return;}
 assert(a>=0x1e980100 && a<=0x1e98017c && !(a&3));
 unsigned o=(a-0x1e980100)/4;
 assert(o==0 || o==1 || o==2 || o==4 || o==5 || o==8 || o==9 || o==16 || o==20);
 if(o==0){
  assert(v==1);Resets++;
  R[0]=(Fail==3 || (Fail==7 && Resets==2))?1:0;
  Ran=0;R[1]=0;R[2]=0;R[3]=0;
 }else if(o==1){
  assert(v<=1);
  if(v){
   assert(Barriers>=2 && R[8]==0x1088a200 && R[9]==(LoopMode?0x1088a203:0x1088a202) && !R[5]);
   assert(Packet[0]==0xa080000012345678ULL && Packet[1]==0x4000000000000000ULL && Packet[2]==(LoopMode?0x100000011088a200ULL:0x1000000000000001ULL));
   Starts++;Ran=1;
  }
  R[o]=v;
 }else if(o==2){assert(v==1);R[2]=1;if(Fail!=8)R[3]|=2;}
 else if(o==4){assert(v==~R[4]);if(Fail!=10)R[4]&=v;}
 else R[o]=v;
}
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/GceFetchSmoke.h"
static const UINT64 Smoke[]={0xa080000012345678ULL,0x4000000000000000ULL,0x1000000000000001ULL};
static const UINT64 LoopSmoke[]={0xa080000012345678ULL,0x4000000000000000ULL,0x100000011088a200ULL};
static BOOLEAN BeforeEnable(void){return Fail!=11;}
static void Progress(UINT32 tick,UINT32 count){assert(tick%5000==0 && count);ProgressCalls++;}
#define GceFetchSmoke(p) GceRunProgram(Smoke,3,0x12345678,p,BeforeEnable,FALSE,NULL,FALSE)
static void reset(unsigned f){
 memset(R,0,sizeof R);memset(Packet,0,sizeof Packet);
 Fail=f;Ticks=Starts=Resets=Writes=Barriers=Snapshot=Ran=Observed=LoopMode=ProgressCalls=0;
 if(f==4)R[24]=0x12345678;
 if(f==5)R[5]=1;
}
int main(void){
 reset(0);assert(GceFetchSmoke(Packet)==0 && Starts==1 && Resets==2 && !R[1] && !R[4]);
 reset(9);assert(GceFetchSmoke(Packet)==0 && Starts==1); // already disabled cleanup
 reset(1);assert(GceFetchSmoke(Packet)==46 && Starts==1 && Ticks==200000);
 reset(2);assert(GceFetchSmoke(Packet)==47 && Starts==1 && !R[4]);
 reset(3);assert(GceFetchSmoke(Packet)==43 && !Starts && Ticks==100);
 reset(4);assert(GceFetchSmoke(Packet)==44 && !Starts);
 reset(5);assert(GceFetchSmoke(Packet)==42 && !Writes);
 reset(6);assert(GceFetchSmoke(Packet)==45 && !Starts);
 reset(7);assert(GceFetchSmoke(Packet)==48 && Starts==1);
 reset(8);assert(GceFetchSmoke(Packet)==48 && Starts==1 && Ticks==1000);
 reset(10);assert(GceFetchSmoke(Packet)==48 && Starts==1 && R[4]==1);
 reset(11);assert(GceFetchSmoke(Packet)==49 && !Starts && Resets==2);
 reset(0);Snapshot=4;assert(GceFetchSmoke(Packet)==41 && !Writes);
 reset(0);assert(GceFetchSmoke(NULL)==40 && !Writes);
 reset(0);LoopMode=1;
 assert(GceRunProgram(LoopSmoke,3,0x12345678,Packet,BeforeEnable,TRUE,Progress,FALSE)==0);
 assert(ProgressCalls==3 && Ticks==2000000 && Resets==2 && !R[1] && !R[4]);
 assert(memcmp(Packet,LoopSmoke,sizeof LoopSmoke)==0);
 for(unsigned f=12;f<=16;f++){
  reset(f);LoopMode=1;
  assert(GceRunProgram(LoopSmoke,3,0x12345678,Packet,BeforeEnable,TRUE,Progress,FALSE)==(f<=13?54:(f<=15?53:47)));
  assert(Ticks<2000000 && Resets==2 && !R[1] && !R[4]);
 }
 reset(0);LoopMode=1;
 assert(GceRunProgram(Smoke,3,0x12345678,Packet,BeforeEnable,TRUE,Progress,FALSE)==40 && !Writes);
 reset(0);LoopMode=1;
 assert(GceRunProgram(LoopSmoke,3,0x12345678,Packet,BeforeEnable,TRUE,NULL,TRUE)==0);
 assert(R[1]==1 && Resets==1 && R[10]>=3 && Ticks<=200000 && !ProgressCalls);
 assert(GceStopProgram()==0 && Resets==2 && !R[1]);
 reset(12);LoopMode=1;
 assert(GceRunProgram(LoopSmoke,3,0x12345678,Packet,BeforeEnable,TRUE,NULL,TRUE)==46);
 assert(Resets==2 && !R[1] && Ticks==200000);
 reset(0);assert(GceRunProgram(Smoke,3,0x12345678,Packet,BeforeEnable,FALSE,NULL,TRUE)==40 && !Writes);
 reset(0);assert(GceRunProgram(LoopSmoke,3,0x12345678,Packet,BeforeEnable,TRUE,Progress,TRUE)==40 && !Writes);
 puts("PASS: actual GCE sentinel flow, exact words/PC/END, strict MMIO write allowlist, preconditions, timeout/error/reset/readback/suspend paths, W0C, already-disabled cleanup; NO HARDWARE");
}
