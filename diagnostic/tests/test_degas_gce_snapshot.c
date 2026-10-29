// Actual snapshot implementation: staged gates, fixed read allowlist, breadcrumbs.
#include <assert.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
typedef uintptr_t UINTN;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef uint16_t UINT16;
#define STATIC static
#define CONST const
#define VOID void
#define DEBUG_ERROR 1
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
static UINT32 Power,Cg0,Cg1,Gctl,Active;
static UINTN Reads[80];static unsigned Count;
static UINT32 Pending;static unsigned Stage;
static UINTN El,Reply;
static unsigned QueryCalls,QueryStage;
typedef struct {
 UINTN Arg0,Arg1,Arg2,Arg3,Arg4,Arg5,Arg6,Arg7;
 UINTN Arg8,Arg9,Arg10,Arg11,Arg12,Arg13,Arg14,Arg15,Arg16,Arg17;
} ARM_SMC_ARGS;
static void TestDebug(unsigned level,const char *f,...){
 (void)level;va_list ap;va_start(ap,f);
 if(strstr(f,"read_begin=")){assert(!Stage);Pending=va_arg(ap,UINT32);Stage=1;}
 else if(strstr(f,"read_value=")){assert(Stage==2 && Pending==va_arg(ap,UINT32));Stage=0;}
 else if(strstr(f,"query_begin")){assert(!Stage && !QueryStage);QueryStage=1;}
 else if(strstr(f,"query_return")){assert(QueryStage==4);QueryStage=0;}
 va_end(ap);
}
#define DEBUG(x) TestDebug x
static void MemoryFence(void){
 if(Stage==1)return;
 assert(QueryStage==1 || QueryStage==3);QueryStage++;
}
static UINTN ArmReadCurrentEL(void){return El;}
static void ArmCallSmc(ARM_SMC_ARGS *q){
 const ARM_SMC_ARGS expected={.Arg0=0xbb00ffa4};
 assert(QueryStage==2 && !Stage && !memcmp(q,&expected,sizeof expected));
 QueryCalls++;QueryStage=3;q->Arg0=Reply;
}
static UINT32 MmioRead32(UINTN a){
 assert(Stage==1 && a==Pending && Count<ARRAY_SIZE(Reads));Stage=2;Reads[Count++]=a;
 if(a==0x1c001eac)return Power;
 if(a==0x1e800100)return Cg0;
 if(a==0x1e800110)return Cg1;
 if(a==0x1e980048)return Gctl;
 if(a>=0x1e980104 && a<=0x1e981084 && (a-0x1e980104)%0x80==0)
  return (Active>>((a-0x1e980104)/0x80))&1;
 static const UINTN Allowed[]={
  0x1e980010,0x1e980018,0x1e980030,0x1e980040,0x1e980054,0x1e9800c0,
  0x1e980100,0x1e980108,0x1e98010c,0x1e980110,0x1e980114,0x1e980120,
  0x1e980124,0x1e980128,0x1e980130,0x1e980140,0x1e980144,0x1e980150,
  0x1e810000,0x1e810008,0x1e810048,0x1e810110};
 for(unsigned i=0;i<ARRAY_SIZE(Allowed);i++)if(a==Allowed[i])return 0;
 assert(!"Read outside snapshot allowlist");return 0;
}
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/GceSnapshot.h"
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/GceMappingQuery.h"
static void reset(void){Count=0;Stage=0;Power=0xc200000d;Cg0=Cg1=Active=0;Gctl=0x20002;
 El=4;Reply=1;QueryCalls=QueryStage=0;}
int main(void){
 reset();assert(GceSnapshot()==0 && Count==58 && !Stage);
 assert(Reads[0]==0x1c001eac && Reads[1]==0x1e800100 && Reads[2]==0x1e800110);
 reset();Power=0;assert(GceSnapshot()==1 && Count==1 && !Stage);
 reset();Power=0x80000000;assert(GceSnapshot()==1 && Count==1);
 reset();Power=0x40000000;assert(GceSnapshot()==1 && Count==1);
 for(unsigned bit=0;bit<3;bit+=2){reset();Cg0=1u<<bit;assert(GceSnapshot()==2 && Count==3);}
 reset();Cg1=1u<<17;assert(GceSnapshot()==2 && Count==3);
 reset();Gctl=0;assert(GceSnapshot()==3 && Count==4);
 reset();Active=1;assert(GceSnapshot()==4 && Count==46);
 reset();Active=0x80000000;assert(GceSnapshot()==4 && Count==46 && !Stage);
 for(UINTN result=0;result<2;result++){
  reset();Reply=result;assert(GceMappingQuery()==0 && QueryCalls==1 && Count==62 && !QueryStage);
 }
 reset();Reply=UINTPTR_MAX;assert(GceMappingQuery()==6 && QueryCalls==1);
 reset();Reply=0xffffffffu;assert(GceMappingQuery()==6 && QueryCalls==1);
 reset();Reply=2;assert(GceMappingQuery()==6 && QueryCalls==1);
 for(UINTN el=0;el<=12;el+=4)if(el!=4){
  reset();El=el;assert(GceMappingQuery()==5 && !QueryCalls && Count==58);
 }
 reset();Power=0;assert(GceMappingQuery()==1 && !QueryCalls);
 reset();Cg0=1;assert(GceMappingQuery()==2 && !QueryCalls);
 reset();Gctl=0;assert(GceMappingQuery()==3 && !QueryCalls);
 reset();Active=1;assert(GceMappingQuery()==4 && !QueryCalls);
 puts("GCE PASS: snapshot allowlist/gates and query-only exact SMC ID/zero arguments, before/after fences, EL1 guard, boolean/unsupported replies; NO HARDWARE");
 return 0;
}
