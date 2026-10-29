// Executes the actual firmware control flow with adversarial SMC/MMIO replies.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint64_t UINT64;typedef uint32_t UINT32;typedef uintptr_t UINTN;
typedef int BOOLEAN;
#define TRUE 1
#define FALSE 0
#define STATIC static
#define CONST const
#define VOID void
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define DEBUG_ERROR 1
static void TestDebug(unsigned level,const char *fmt,...){(void)level;(void)fmt;}
#define DEBUG(a) TestDebug a
#define ZeroMem(p,n) memset(p,0,n)
typedef struct {UINTN Arg0,Arg1,Arg2,Arg3,Arg4,Arg5,Arg6,Arg7,
 Arg8,Arg9,Arg10,Arg11,Arg12,Arg13,Arg14,Arg15,Arg16,Arg17;} ARM_SMC_ARGS;
static unsigned Calls,Writes,Syncs,Snapshot,El,Type2,FailQuery,Occupied,BadPage,BadMarker,Barriers;
static UINT64 FailValue,SyncReply;
static UINT32 Root[16384],Leaf[256];
static void MemoryFence(void){}
static void ArmDataSynchronizationBarrier(void){Barriers++;}
static UINTN ArmReadCurrentEL(void){return El;}
static UINT32 GceSnapshot(void){return Snapshot;}
static void MmioWrite32(UINTN a,UINT32 v){
 assert(a==0x1e810000 && v==0x44440000 && Calls==259 && !Writes && Barriers>=2);
 assert(Root[0x444]==0x44450009 && Leaf[0x51]==0x44451c42);Writes++;
}
static UINT32 MmioRead32(UINTN a){assert(a==0x1e810000 && Writes==1);return 0xc0000007;}
static void ArmCallSmc(ARM_SMC_ARGS *q){
 assert(!q->Arg3 && !q->Arg4 && !q->Arg5 && !q->Arg6 && !q->Arg7 &&
        !q->Arg8 && !q->Arg9 && !q->Arg10 && !q->Arg11 && !q->Arg12 &&
        !q->Arg13 && !q->Arg14 && !q->Arg15 && !q->Arg16 && !q->Arg17);
 if(q->Arg0==0xbb00ffa4){assert(!q->Arg1 && !q->Arg2);q->Arg0=Type2;return;}
 if(q->Arg0==0xbb00ffa7){
  assert(Writes==1 && !Syncs && q->Arg1==0x44451000 && q->Arg2==0x44451000 && Barriers>=4);
  Syncs++;q->Arg0=SyncReply;return;
 }
 assert(q->Arg0==0xbb00ffa6);Calls++;
 UINT64 a=q->Arg1|((UINT64)q->Arg2<<32);
 if(Calls==FailQuery){q->Arg0=FailValue;return;}
 if(a>=0x44400000 && a<0x44500000){
  unsigned page=(a-0x44400000)>>12;assert(!(a&4095));
  if(!Syncs){assert(Calls==page+1);q->Arg0=Occupied==page+1?0x88880a00ULL<<32:0;}
  else {
   assert(Calls==260+page);q->Arg0=page==0x51?0x44451a00:0;
   if(BadPage==page+1)q->Arg0^=0x1000;
   if(BadPage==257 && page==0x51)q->Arg0|=0x88880a00ULL<<32;
   if(BadPage==258 && page==0x51)q->Arg0^=0x400; // wrong level
   if(BadPage==259 && page==255)q->Arg0=0x200; // extra mapped leaf
  }
 }else {
  assert(a==0x107400000ULL || a==0x1074ff000ULL || a==0xfcc2f000);
  // Valid high protected descriptor makes the entire X0 signed-negative.
  q->Arg0=0x88880a0300000000ULL;
  if(BadMarker && Syncs && a==0x107400000ULL)q->Arg0^=0x100000000ULL;
 }
}
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/GceMappingInstall.h"
static void reset(void){
 Calls=Writes=Syncs=Snapshot=FailQuery=Occupied=BadPage=BadMarker=Barriers=0;
 El=4;Type2=1;SyncReply=0;FailValue=UINT64_MAX-2;
 memset(Root,0xab,sizeof Root);memset(Leaf,0xab,sizeof Leaf);
}
int main(void){
 reset();assert(GceMappingInstall(Root,Leaf)==0 && Calls==518 && Writes==1 && Syncs==1);
 for(unsigned p=1;p<=259;p++){
  reset();FailQuery=p;assert(GceMappingInstall(Root,Leaf)!=0 && !Writes && !Syncs);
  assert(Root[0]==0xabababab && Leaf[0]==0xabababab);
 }
 for(unsigned p=1;p<=256;p++){
  reset();Occupied=p;assert(GceMappingInstall(Root,Leaf)==14 && !Writes);
 }
 for(unsigned p=260;p<=518;p++){
  reset();FailQuery=p;assert(GceMappingInstall(Root,Leaf)!=0 && Writes==1 && Syncs==1);
 }
 for(unsigned i=0;i<6;i++){
  reset();FailQuery=1;FailValue=i<3?0ULL-(i+1):(UINT32)(0U-(i-2));
  assert(GceMappingInstall(Root,Leaf)==13 && !Writes);
 }
 reset();BadPage=0x52;assert(GceMappingInstall(Root,Leaf)==19);
 reset();BadPage=257;assert(GceMappingInstall(Root,Leaf)==19);
 reset();BadPage=258;assert(GceMappingInstall(Root,Leaf)==19);
 reset();BadPage=259;assert(GceMappingInstall(Root,Leaf)==30);
 reset();BadMarker=1;assert(GceMappingInstall(Root,Leaf)==32);
 reset();SyncReply=UINT64_MAX;assert(GceMappingInstall(Root,Leaf)==17 && Calls==259);
 reset();El=8;assert(GceMappingInstall(Root,Leaf)==11 && !Calls && !Writes);
 reset();Snapshot=4;assert(GceMappingInstall(Root,Leaf)==24 && !Calls && !Writes);
 reset();Type2=0;assert(GceMappingInstall(Root,Leaf)==12 && !Calls && !Writes);
 reset();assert(GceMappingInstall(NULL,Leaf)==10 && !Calls && !Writes);
 puts("PASS: actual mapping-only control flow, all 518 query failure points, occupied granule guards, wrong PA/level/protected/neighbor/marker, exact SMC ABI, one root write only, NO HARDWARE");
}
