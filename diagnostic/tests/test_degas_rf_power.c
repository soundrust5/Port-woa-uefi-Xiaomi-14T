#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/PmicRead.h"
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/RfPmicUpdate.h"
#include "../degas/wifi/rf_power_core.h"
typedef struct {unsigned char Reg[65536],Original[65536];unsigned Phase,Address,Data,Writes,IgnoreWrite,Commands,Fault,Ramp;} MODEL;
static unsigned rd(void *C,unsigned A){
 MODEL *M=C;
 if(A==0)return M->Fault==1?0:1;
 if(A==0x8a8)return M->Fault==2?12:M->Phase?(M->Fault==3?2:12):0;
 assert(A==0x894 && M->Phase);return M->Reg[M->Address];
}
static void wr(void *C,unsigned A,unsigned V){
 MODEL *M=C;
 if(A==0x884){assert(!M->Phase);M->Data=V;return;}
 if(A==0x8a4){assert(M->Phase && V==1);M->Phase=0;return;}
 assert(A==0x880 && !M->Phase);M->Commands++;M->Address=V&65535;
 assert((V&0xff000000u)==0xc4000000u || (V&0xff000000u)==0xe4000000u);
 if((V&0xff000000u)==0xc4000000u){M->Phase=1;return;}
 M->Writes++;if(M->Writes!=M->IgnoreWrite)M->Reg[M->Address]=(unsigned char)M->Data;
}
static void us(void *C,unsigned U){(void)C;assert(U==10);}
static unsigned read_byte(void *C,unsigned A,unsigned *V){DEGAS_PMIF_IO I={C,rd,wr,us};return DegasPmicReadByte(&I,4,A,V);}
static unsigned update(void *C,unsigned A,unsigned Mask,unsigned V){DEGAS_PMIF_IO I={C,rd,wr,us};return DegasPmicRfUpdate(&I,A,Mask,V);}
static void ramp(void *C,unsigned U){MODEL *M=C;assert(U==180);M->Ramp++;}
static MODEL cold(void){
 MODEL M={0};M.Reg[0x1f2c]=0xac;M.Reg[0x1bcd]=0xa0;M.Reg[0x1d07]=0x5c;
 M.Reg[0x1bd4]=0x93;M.Reg[0x1d16]=0xe1;M.Reg[0x1b3f]=0xe4;M.Reg[0x1f88]=0xf5;M.Reg[0x1f2b]=0xd2;
 memcpy(M.Original,M.Reg,sizeof(M.Reg));return M;
}
int main(void){
 MODEL M=cold();DEGAS_RF_IO I={&M,read_byte,update,ramp,0};DEGAS_RF_RESULT R;
 DegasRfPrepare(&I,&R);assert(!R.Code && R.Ready && R.Owned && !R.Restored && M.Writes==3 && M.Ramp==2);
 assert(M.Reg[0x1bcd]==0xa1 && M.Reg[0x1d07]==0x5d && M.Reg[0x1f2c]==0xab);
 assert(DegasRfRestore(&I,&R) && R.Restored && M.Writes==6 && !memcmp(M.Reg,M.Original,sizeof(M.Reg)));
 for(unsigned N=1;N<=3;N++){
  M=cold();M.IgnoreWrite=N;DegasRfPrepare(&I,&R);
  assert(R.Code==1 && R.Owned && !R.Ready && !R.Restored);
  M.IgnoreWrite=0;assert(DegasRfRestore(&I,&R));assert(!memcmp(M.Reg,M.Original,sizeof(M.Reg)));
 }
 for(unsigned N=0;N<5;N++){
  M=cold();if(N==0)M.Reg[0x1bcd]|=1;if(N==1)M.Reg[0x1d07]|=1;
  if(N==2)M.Reg[0x1f88]=4;if(N==3)M.Reg[0x1f2b]=3;if(N==4)M.Reg[0x1f2c]=0xa9;
  DegasRfPrepare(&I,&R);assert(R.Code==2 && !R.Owned && !M.Writes);
 }
 M=cold();DegasRfPrepare(&I,&R);M.IgnoreWrite=M.Writes+1;
 assert(!DegasRfRestore(&I,&R) && R.Code==3 && !R.Restored);
 for(unsigned F=1;F<=3;F++){
  M=cold();M.Fault=F;DegasRfPrepare(&I,&R);assert(R.Code==1 && !R.Owned && !M.Writes);
 }
 M=cold();assert(update(&M,0x1f88,15,5)==5 && !M.Commands);
 assert(update(&M,0x1f2c,16,0)==5 && !M.Commands);
 assert(update(&M,0x1bcd,3,4)==5 && !M.Commands);
 assert(update(&M,0x1e,1,1)==5 && !M.Commands);
 puts("PASS: scoped SID4 writes, voltage/enable ordering, 180us ramps, calibration guards, fault cleanup and exact restore");
}
