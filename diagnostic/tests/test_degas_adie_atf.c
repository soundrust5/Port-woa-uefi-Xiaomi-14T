#include <assert.h>
#include <stdio.h>
#include "../degas/wifi/adie_atf_core.h"
typedef struct {unsigned N,Fail,Transport,BadId,BadRead,OffCalls,InitMinusOne;} MODEL;
static const unsigned Ops[]={0,10,70,11,12,13,90,20,90};
static int call(void *C,unsigned Op,unsigned A,unsigned B,uint64_t V[4]){
 MODEL *M=C;unsigned N=M->N++;
 unsigned Slot=N;if(M->Fail==7 && N==7)Slot=8; // clock-on failed: skip read, still disable clock.
 assert(Slot<9 && Op==Ops[Slot]);
 if(Op==0)assert(A==0 && B==1);
 if(Op==12 || Op==13)assert(A==0 && B==1);
 if(Op==20)assert(A==5 && B==0x2c);
 if(Op==90){assert(A==(N==6) && !B);if(!A)M->OffCalls++;}
 if(Op==10 || Op==70 || Op==11)assert(!A && !B);
 if(M->Fail==N+1){V[0]=(Op==0 || Op==90)?UINT64_MAX-1:UINT64_MAX;return !M->Transport;}
 V[0]=0;
 if((Op==0 || Op==90) && M->InitMinusOne)V[0]=UINT32_MAX;
 if(Op==13)V[1]=M->BadId?0x66350000:0x66378a01;
 if(Op==20)V[1]=M->BadRead?0:0x66378a01;
 return 1;
}
int main(void){
 MODEL M={0};DEGAS_ADIE_IO Io={&M,call};DEGAS_ADIE_RESULT R;
 assert(DegasAdieInitialize(&Io,&R) && DegasAdieIdentify(&Io,&R));
 assert(M.N==9 && M.OffCalls==1 && R.Id==0x66378a01 && R.ReadId==R.Id && !R.ClockOffFailed);
 for(unsigned Fail=1;Fail<=9;Fail++){
  M=(MODEL){.Fail=Fail};Io.Context=&M;
  int Ok=DegasAdieInitialize(&Io,&R);if(Ok)Ok=DegasAdieIdentify(&Io,&R);
  assert(!Ok && R.Code && R.Touched);
  assert(M.OffCalls==(Fail>=7));
 }
 M=(MODEL){.BadId=1};Io.Context=&M;
 assert(DegasAdieInitialize(&Io,&R) && !DegasAdieIdentify(&Io,&R) && M.N==6 && R.Code==2);
 M=(MODEL){.BadRead=1};Io.Context=&M;
 assert(DegasAdieInitialize(&Io,&R) && !DegasAdieIdentify(&Io,&R) && M.OffCalls==1 && R.Code==2);
 M=(MODEL){.Fail=1,.Transport=1};Io.Context=&M;
 assert(!DegasAdieInitialize(&Io,&R) && R.Code==3);
 M=(MODEL){.InitMinusOne=1};Io.Context=&M;
 assert(DegasAdieInitialize(&Io,&R) && R.LastStatus==UINT32_MAX && !R.Code);
 assert(DegasAdieIdentify(&Io,&R) && R.ReadId==0x66378a01);
 puts("PASS: exact SMC order/arguments, config and independent RF ID, aborts, clock cleanup");
}
