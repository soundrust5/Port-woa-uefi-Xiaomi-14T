#include "../../../../../../diagnostic/degas/wifi/adie_atf_core.h"
STATIC int WifiAdieCall(void *Context,unsigned Op,unsigned A,unsigned B,uint64_t Out[4]){
 ARM_SMC_ARGS S={0};BOOLEAN Allowed=FALSE;(VOID)Context;
 switch(Op){
 case 0:Allowed=A==0 && B==1;break;
 case 10:case 70:case 11:Allowed=A==0 && B==0;break;
 case 12:case 13:Allowed=A==0 && B==1;break;
 case 90:Allowed=A<=1 && B==0;break;
 case 20:Allowed=A==5 && B==0x2c;break;
 default:break;
 }
 if(!Allowed)return 0;
 S.Arg0=0xc2000534;S.Arg1=Op;S.Arg2=A;S.Arg3=B;
 MmioWrite32(0x1c00a008,0x1971);
 DEBUG((DEBUG_ERROR,"DEGAS ADIE SMC BEGIN op=%u a=%u b=%u\n",Op,A,B));
 ArmCallSmc(&S);
 Out[0]=S.Arg0;Out[1]=S.Arg1;Out[2]=S.Arg2;Out[3]=S.Arg3;
 DEBUG((DEBUG_ERROR,"DEGAS ADIE SMC END op=%u x0=%016lx x1=%016lx x2=%016lx x3=%016lx\n",Op,S.Arg0,S.Arg1,S.Arg2,S.Arg3));
 return 1;
}
STATIC int WifiAdieActive(void *Context,CONST DEGAS_CONN_RESULT *Conn){
 DEGAS_ADIE_RESULT *R=Context;DEGAS_ADIE_IO Io={NULL,WifiAdieCall};
 if(Conn->Version!=0x02050400 || !Conn->Owned)return 0;
 return DegasAdieIdentify(&Io,R);
}
