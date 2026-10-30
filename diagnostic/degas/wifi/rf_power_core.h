#ifndef DEGAS_RF_POWER_CORE_H
#define DEGAS_RF_POWER_CORE_H
#include <stdint.h>
typedef struct {
 void *Context;
 unsigned (*Read)(void *,unsigned,unsigned *);
 unsigned (*Update)(void *,unsigned,unsigned,unsigned);
 void (*Delay)(void *,unsigned);
 void (*Log)(void *,unsigned,unsigned,unsigned);
} DEGAS_RF_IO;
typedef struct {unsigned Code,Owned,Ready,Restored,Before[5];} DEGAS_RF_RESULT;
static const unsigned RfAddress[5]={0x1f2c,0x1bd4,0x1bcd,0x1d16,0x1d07};
static const unsigned RfMask[5]={15,128,3,128,3};
static inline unsigned RfRead(const DEGAS_RF_IO *Io,unsigned A,unsigned *V){
 unsigned E=Io->Read(Io->Context,A,V);if(Io->Log)Io->Log(Io->Context,A,E?0:*V,E);return E;
}
static inline void DegasRfPrepare(const DEGAS_RF_IO *Io,DEGAS_RF_RESULT *R){
 unsigned I,Vcn,VcnCal,RfCal,V;
 *R=(DEGAS_RF_RESULT){0};R->Restored=1;
 for(I=0;I<5;I++)if(RfRead(Io,RfAddress[I],&R->Before[I])){R->Code=1;return;}
 if(RfRead(Io,0x1b3f,&Vcn)||RfRead(Io,0x1f88,&VcnCal)||RfRead(Io,0x1f2b,&RfCal)){R->Code=1;return;}
 /* This private test is qualified only for this phone's observed cold state.
  * Refuse enabled/shared rails and unknown selector/calibration baselines. */
 if((R->Before[2]&3)||(R->Before[4]&3)||
    ((R->Before[0]&15)!=12 && (R->Before[0]&15)!=11)||
    (Vcn&15)!=4||(VcnCal&15)!=5||(RfCal&15)!=2){R->Code=2;return;}
 R->Owned=1;R->Restored=0;
 /* Match installed common_power_ctrl: VRFIO18 nominal1.7V, op config,
  * enable, NORMAL; then VCN13 op config, enable, NORMAL. VCN13 selector and
  * both factory calibration fields are deliberately preserved. */
 if(Io->Update(Io->Context,0x1f2c,15,11)||Io->Update(Io->Context,0x1bd4,128,128)||
    Io->Update(Io->Context,0x1bcd,1,1)){R->Code=1;return;}
 Io->Delay(Io->Context,180); // live DT regulator-enable-ramp-delay, microseconds.
 if(Io->Update(Io->Context,0x1bcd,2,0)||Io->Update(Io->Context,0x1d16,128,128)||
    Io->Update(Io->Context,0x1d07,1,1)){R->Code=1;return;}
 Io->Delay(Io->Context,180);
 if(Io->Update(Io->Context,0x1d07,2,0)){R->Code=1;return;}
 for(I=0;I<5;I++){
  unsigned Expected=I==0?11:(I==1||I==3)?128:1;
  if(RfRead(Io,RfAddress[I],&V)||(V&RfMask[I])!=Expected){R->Code=1;return;}
 }
 R->Ready=1;
}
static inline unsigned DegasRfRestore(const DEGAS_RF_IO *Io,DEGAS_RF_RESULT *R){
 unsigned N,I,V;
 if(!R->Owned)return R->Restored;
 /* Only after CONN is off: disable VCN13, restore its op field, disable
  * VRFIO18, restore op field, then restore the original OFF voltage selector. */
 for(N=0;N<5;N++){
  I=4-N;
  if(Io->Update(Io->Context,RfAddress[I],RfMask[I],R->Before[I]&RfMask[I])||
     RfRead(Io,RfAddress[I],&V)||(V&RfMask[I])!=(R->Before[I]&RfMask[I])){
   R->Code=3;return 0;
  }
 }
 R->Restored=1;return 1;
}
#endif
