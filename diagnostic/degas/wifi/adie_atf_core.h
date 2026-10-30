#ifndef DEGAS_ADIE_ATF_CORE_H
#define DEGAS_ADIE_ATF_CORE_H
#include <stdint.h>
typedef struct {void *Context;int (*Call)(void *,unsigned,unsigned,unsigned,uint64_t[4]);} DEGAS_ADIE_IO;
typedef struct {unsigned Code,Stage,Touched,ClockOffFailed;uint32_t Id,ReadId;uint64_t LastStatus;} DEGAS_ADIE_RESULT;
static inline int AdieCall(const DEGAS_ADIE_IO *Io,DEGAS_ADIE_RESULT *R,unsigned Op,unsigned A,unsigned B,uint64_t Out[4]){
 unsigned I;for(I=0;I<4;I++)Out[I]=0;R->Stage=Op;R->Touched=1;
 if(!Io->Call(Io->Context,Op,A,B,Out)){R->Code=3;return 0;}
 R->LastStatus=Out[0];
 // In exact stock ATF ef838aa7..., OP0 and OP90 execute their side effects
 // then unconditionally return w0=-1. No other opcode has this exception.
 if((uint32_t)Out[0] && !((Op==0 || Op==90) && (uint32_t)Out[0]==UINT32_MAX)){
  R->Code=1;return 0;
 }
 return 1;
}
static inline int DegasAdieInitialize(const DEGAS_ADIE_IO *Io,DEGAS_ADIE_RESULT *R){
 uint64_t V[4];*R=(DEGAS_ADIE_RESULT){0};
 // Live native trace: platform config 0, co-clock enum 1 (52 MHz).
 return AdieCall(Io,R,0,0,1,V);
}
static inline int DegasAdieIdentify(const DEGAS_ADIE_IO *Io,DEGAS_ADIE_RESULT *R){
 uint64_t V[4];unsigned Code,Stage;uint64_t Status;
 // Native cold first-subsystem trace: current=0,next=1. This is shared
 // CONNINFRA initialization, not a request to start the Bluetooth firmware.
 if(!AdieCall(Io,R,10,0,0,V) || !AdieCall(Io,R,70,0,0,V) ||
    !AdieCall(Io,R,11,0,0,V) || !AdieCall(Io,R,12,0,1,V) ||
    !AdieCall(Io,R,13,0,1,V))return 0;
 R->Id=(uint32_t)V[1];if(R->Id!=UINT32_C(0x66378a01)){R->Code=2;return 0;}
 // An independent RF-SPI read confirms the returned configuration ID.
 // Once clock-on is attempted, always attempt clock-off, even on error.
 if(AdieCall(Io,R,90,1,0,V) && AdieCall(Io,R,20,5,0x2c,V)){
  R->ReadId=(uint32_t)V[1];if(R->ReadId!=R->Id)R->Code=2;
 }
 Code=R->Code;Stage=R->Stage;Status=R->LastStatus;
 if(!AdieCall(Io,R,90,0,0,V))R->ClockOffFailed=1;
 if(Code){R->Code=Code;R->Stage=Stage;R->LastStatus=Status;}
 return !R->Code && !R->ClockOffFailed;
}
#endif
