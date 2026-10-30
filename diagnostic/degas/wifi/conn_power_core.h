/* MT6897 CONN genpd only. No RF PMIC, ATF, firmware, DMA or WFSYS writes.
 * Selected Linux SCPSYS domain1: E04, no clocks/supply/SRAM fields.
 * Bus-protection provider resolved from live DT ifr-bus to1002c000.
 */
#ifndef DEGAS_CONN_POWER_CORE_H
#define DEGAS_CONN_POWER_CORE_H
#include <stdint.h>
typedef struct {
 void *Context;
 int (*Read)(void *,uint64_t,uint32_t *);
 int (*Write)(void *,uint64_t,uint32_t);
 void (*Delay)(void *,unsigned);
 void (*Log)(void *,unsigned,uint64_t,uint32_t);
} DEGAS_CONN_IO;
typedef struct {
 unsigned Code,Stage,Writes,Owned,Restored;
 uint32_t Config,Before,After,Enable[2],Protect[2],Mirror[2],Version,Ownership;
 uint32_t PinsBefore[4];unsigned PinsOwned,PinsRestored;
} DEGAS_CONN_RESULT;
#define CONN_CTL UINT64_C(0x1c001e04)
#define CONN_CFG UINT64_C(0x1c001000)
#define CONN_BUS UINT64_C(0x1002c000)
#define CONN_I_MASK UINT32_C(0x06000000)
#define CONN_C_MASK UINT32_C(3)
static const uint32_t ConnBpOffset[4]={4,0x1c4,4,0x1c4};
static const uint32_t ConnBpMask[4]={UINT32_C(1)<<25,2,UINT32_C(1)<<26,1};
/* Exact installed consys_set_if_pinmux_mt6897_gen(enable=1). GPIO192..202
 * are mode1 in the Android hardware capture. Preserve every unowned bit. */
static const uint64_t ConnPinAddress[4]={UINT64_C(0x10005480),UINT64_C(0x10005490),UINT64_C(0x11b20030),UINT64_C(0x11b200d0)};
static const uint32_t ConnPinMask[4]={UINT32_C(0x77777777),UINT32_C(0x777),UINT32_C(0x1ff000),10};
static const uint32_t ConnPinValue[4]={UINT32_C(0x11111111),UINT32_C(0x111),UINT32_C(0x49000),0};
enum {ConnOk,ConnIoError,ConnUnexpectedState,ConnSpmLocked,ConnAckTimeout,
 ConnProtectionTimeout,ConnVersionMismatch,ConnRestoreFailed};
static inline int ConnRead(const DEGAS_CONN_IO *io,uint64_t a,uint32_t *v){return io->Read(io->Context,a,v);}
static inline int ConnWrite(const DEGAS_CONN_IO *io,DEGAS_CONN_RESULT *r,uint64_t a,uint32_t v){
 if(!io->Write(io->Context,a,v))return 0;r->Writes++;return 1;
}
static inline void ConnStage(const DEGAS_CONN_IO *io,DEGAS_CONN_RESULT *r,unsigned s,uint64_t a,uint32_t v){
 r->Stage=s;if(io->Log)io->Log(io->Context,s,a,v);
}
static inline int ConnRmw(const DEGAS_CONN_IO *io,DEGAS_CONN_RESULT *r,uint32_t clear,uint32_t set){
 uint32_t v;return ConnRead(io,CONN_CTL,&v) && ConnWrite(io,r,CONN_CTL,(v&~clear)|set);
}
static inline int ConnPoll(const DEGAS_CONN_IO *io,uint64_t a,uint32_t mask,uint32_t want){
 unsigned i;uint32_t v;
 for(i=0;i<100000;i++){
  if(!ConnRead(io,a,&v))return 0;
  if((v&mask)==want)return 1;
  io->Delay(io->Context,10);
 }return 0;
}
static inline int ConnPinsPrepare(const DEGAS_CONN_IO *io,DEGAS_CONN_RESULT *r){
 unsigned i;uint32_t v;
 for(i=0;i<4;i++){
  if(!ConnRead(io,ConnPinAddress[i],&r->PinsBefore[i]))return 0;
  ConnStage(io,r,3,ConnPinAddress[i],r->PinsBefore[i]);
 }
 r->PinsOwned=1;
 for(i=0;i<4;i++){
  v=(r->PinsBefore[i]&~ConnPinMask[i])|ConnPinValue[i];
  if(!ConnWrite(io,r,ConnPinAddress[i],v) ||
     !ConnRead(io,ConnPinAddress[i],&v) || (v&ConnPinMask[i])!=ConnPinValue[i])return 0;
 }
 return 1;
}
static inline int ConnPinsRestore(const DEGAS_CONN_IO *io,DEGAS_CONN_RESULT *r){
 unsigned n,i;uint32_t v;
 for(n=0;n<4;n++){
  i=3-n;
  if(!ConnRead(io,ConnPinAddress[i],&v) ||
     !ConnWrite(io,r,ConnPinAddress[i],(v&~ConnPinMask[i])|(r->PinsBefore[i]&ConnPinMask[i])) ||
     !ConnRead(io,ConnPinAddress[i],&v) ||
     (v&ConnPinMask[i])!=(r->PinsBefore[i]&ConnPinMask[i]))return 0;
 }
 r->PinsRestored=1;return 1;
}
static inline int ConnProtect(const DEGAS_CONN_IO *io,DEGAS_CONN_RESULT *r,int enable){
 unsigned n,i;
 /* Linux set: forward table order. Linux clear: reverse table order. */
 for(n=0;n<4;n++){
  i=enable?n:3-n;
  if(!ConnWrite(io,r,CONN_BUS+ConnBpOffset[i]+(enable?0:4),ConnBpMask[i]))return 0;
  if(!ConnPoll(io,CONN_BUS+(i&1?0x1c0:0),ConnBpMask[i],enable?ConnBpMask[i]:0))return 0;
  if(enable && !ConnPoll(io,CONN_BUS+(i&1?0x1cc:0xc),ConnBpMask[i],ConnBpMask[i]))return 0;
 }
 /* BUS_PROT_IGN: clear acknowledgement is deliberately not polled. Android
  * leaves RDY bits set with EN clear. Poll only EN readback on clear. */
 return 1;
}
static inline int ConnRestore(const DEGAS_CONN_IO *io,DEGAS_CONN_RESULT *r){
 uint32_t a,b;
 ConnStage(io,r,90,CONN_CTL,0);
 if(!ConnProtect(io,r,1))return 0;
 // Linux power-off order: isolation, reset, clock-disable, first/second power.
 if(!ConnRmw(io,r,0,2) || !ConnRmw(io,r,1,0) || !ConnRmw(io,r,0,16) ||
    !ConnRmw(io,r,4,0) || !ConnPoll(io,CONN_CTL,UINT32_C(1)<<30,0) ||
    !ConnRmw(io,r,8,0) || !ConnPoll(io,CONN_CTL,UINT32_C(1)<<31,0))return 0;
 if(!ConnRead(io,CONN_CTL,&r->After) || (r->After&UINT32_C(0xc000001f))!=(r->Before&UINT32_C(0xc000001f)))return 0;
 if(!ConnRead(io,CONN_BUS,&a) || !ConnRead(io,CONN_BUS+0x1c0,&b))return 0;
 /* Finish in the Linux powered-off, fully protected state. LK can initially
  * leave EN clear; do not recreate that by removing protection while off.
  * Every owned transition requires a reset before another boot target. */
 return (a&CONN_I_MASK)==CONN_I_MASK && (b&CONN_C_MASK)==CONN_C_MASK;
}
/* Always-on observation only. In particular, do not infer that RDY_STA must
 * clear with EN: MT6897 uses BUS_PROT_IGN and Android can retain RDY bits. */
static inline void DegasConnAonSnapshot(const DEGAS_CONN_IO *io,DEGAS_CONN_RESULT *r){
 *r=(DEGAS_CONN_RESULT){0};r->Restored=1;
 ConnStage(io,r,1,CONN_CTL,0);
 if(!ConnRead(io,CONN_CFG,&r->Config) || !ConnRead(io,CONN_CTL,&r->Before) ||
    !ConnRead(io,CONN_BUS,&r->Enable[0]) || !ConnRead(io,CONN_BUS+0x1c0,&r->Enable[1]) ||
    !ConnRead(io,CONN_BUS+0xc,&r->Protect[0]) || !ConnRead(io,CONN_BUS+0x1cc,&r->Protect[1]) ||
    !ConnRead(io,UINT64_C(0x1000100c),&r->Mirror[0]) ||
    !ConnRead(io,UINT64_C(0x100011cc),&r->Mirror[1]))r->Code=ConnIoError;
 r->After=r->Before;
 ConnStage(io,r,2,CONN_CTL,r->Before);
}
typedef int (*DEGAS_CONN_ACTIVE)(void *,const DEGAS_CONN_RESULT *);
static inline void DegasConnPowerProbeActive(const DEGAS_CONN_IO *io,DEGAS_CONN_RESULT *r,DEGAS_CONN_ACTIVE Active,void *ActiveContext){
 unsigned i;uint32_t state,initialI,initialC;
 *r=(DEGAS_CONN_RESULT){0};r->Restored=1;
 ConnStage(io,r,1,CONN_CTL,0);
 if(!ConnRead(io,CONN_CFG,&r->Config) || !ConnRead(io,CONN_CTL,&r->Before) ||
    !ConnRead(io,CONN_BUS,&r->Enable[0]) || !ConnRead(io,CONN_BUS+0x1c0,&r->Enable[1]) ||
    !ConnRead(io,CONN_BUS+0xc,&r->Protect[0]) || !ConnRead(io,CONN_BUS+0x1cc,&r->Protect[1])){r->Code=ConnIoError;return;}
 state=r->Before&UINT32_C(0xc000001f);
 ConnStage(io,r,2,CONN_CTL,r->Before);
 if(state==0x12){
  if(!(r->Config&1)){r->Code=ConnSpmLocked;return;}
  initialI=r->Enable[0]&CONN_I_MASK;initialC=r->Enable[1]&CONN_C_MASK;
  /* Accept the observed LK baseline (all owned EN clear), or the normal
   * fully protected off state. Reject a partially configured baseline. */
  if(!((initialI==0 && initialC==0) ||
       (initialI==CONN_I_MASK && initialC==CONN_C_MASK))){r->Code=ConnUnexpectedState;return;}
  r->Owned=1;r->Restored=0;
  if(!ConnPinsPrepare(io,r)){r->Code=ConnIoError;goto Done;}
  ConnStage(io,r,10,CONN_CTL,r->Before);
  if(!ConnRmw(io,r,0,4) || !ConnPoll(io,CONN_CTL,UINT32_C(1)<<30,UINT32_C(1)<<30)){r->Code=ConnAckTimeout;goto Done;}
  io->Delay(io->Context,50);
  if(!ConnRmw(io,r,0,8) || !ConnPoll(io,CONN_CTL,UINT32_C(1)<<31,UINT32_C(1)<<31)){r->Code=ConnAckTimeout;goto Done;}
  if(!ConnRmw(io,r,16,0) || !ConnRmw(io,r,2,0) || !ConnRmw(io,r,0,1)){r->Code=ConnIoError;goto Done;}
  ConnStage(io,r,20,CONN_CTL,0);
  if(!ConnProtect(io,r,0)){r->Code=ConnProtectionTimeout;goto Done;}
 }else if(state!=UINT32_C(0xc000000d) || (r->Enable[0]&CONN_I_MASK) || (r->Enable[1]&3)){
  r->Code=ConnUnexpectedState;return;
 }
 // Mirrors are the exact installed conninfra on-domain readability checks.
 if(!ConnRead(io,UINT64_C(0x1000100c),&r->Mirror[0]) || !ConnRead(io,UINT64_C(0x100011cc),&r->Mirror[1])){r->Code=ConnIoError;goto Done;}
 if((r->Mirror[0]&(UINT32_C(1)<<25)) || (r->Mirror[1]&1)){r->Code=ConnProtectionTimeout;goto Done;}
 ConnStage(io,r,30,UINT64_C(0x18011000),0);
 for(i=0;i<11;i++){
  if(i)io->Delay(io->Context,1000);
  if(!ConnRead(io,UINT64_C(0x18011000),&r->Version)){r->Code=ConnIoError;goto Done;}
  ConnStage(io,r,30,UINT64_C(0x18011000),r->Version);
  if(r->Version==UINT32_C(0x02050400))break;
 }
 if(r->Version!=UINT32_C(0x02050400)){r->Code=ConnVersionMismatch;goto Done;}
 ConnStage(io,r,31,UINT64_C(0x18060010),r->Version);
 if(!ConnRead(io,UINT64_C(0x18060010),&r->Ownership)){r->Code=ConnIoError;goto Done;}
 // The extension runs only inside our own powered interval, after both ID
 // and host CSR reads. Every return still passes through existing cleanup.
 if(Active && (!r->Owned || !Active(ActiveContext,r)))r->Code=ConnIoError;
Done:
 i=r->Stage;
 if(r->Owned){
  r->Restored=ConnRestore(io,r);
  if(r->Restored && r->PinsOwned)r->Restored=ConnPinsRestore(io,r);
  if(!r->Restored)r->Code=ConnRestoreFailed;
 }
 else ConnRead(io,CONN_CTL,&r->After);
 ConnStage(io,r,100+i,CONN_CTL,r->After);
}
static inline void DegasConnPowerProbe(const DEGAS_CONN_IO *io,DEGAS_CONN_RESULT *r){
 DegasConnPowerProbeActive(io,r,0,0);
}
#endif
