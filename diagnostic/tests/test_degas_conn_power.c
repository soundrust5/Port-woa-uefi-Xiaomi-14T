#include <assert.h>
#include <stdio.h>
#include "../degas/wifi/conn_power_core.h"
typedef struct {
 uint32_t ctl,cfg,en[2],foreign[2],version,statusOr[2],mirrorOr[2];
 uint32_t pin[4],pinInitial[4];unsigned pinFault,versionReadyAfter;
 unsigned ackFault,clearFault,restoreFault,versionReads,ownReads,delays,writes;
 uint64_t address[64];uint32_t value[64];
} MODEL;
static int read_reg(void *p,uint64_t a,uint32_t *v){
 MODEL *m=p;
 for(unsigned i=0;i<4;i++)if(a==ConnPinAddress[i]){*v=m->pin[i];return 1;}
 if(a==CONN_CFG)*v=m->cfg;
 else if(a==CONN_CTL)*v=m->ctl;
 else if(a==CONN_BUS+0xc)*v=m->en[0]|m->statusOr[0];
 else if(a==CONN_BUS+0x1cc)*v=m->en[1]|m->statusOr[1];
 else if(a==UINT64_C(0x1000100c))*v=m->en[0]|m->mirrorOr[0];
 else if(a==UINT64_C(0x100011cc))*v=m->en[1]|m->mirrorOr[1];
 else if(a==CONN_BUS)*v=m->en[0];
 else if(a==CONN_BUS+0x1c0)*v=m->en[1];
 else if(a==UINT64_C(0x18011000) || a==UINT64_C(0x18060010)){
  assert((m->ctl&UINT32_C(0xc000001f))==UINT32_C(0xc000000d));
  assert(!(m->en[0]&CONN_I_MASK) && !(m->en[1]&3));
  if(a==UINT64_C(0x18011000)){m->versionReads++;*v=m->versionReads<=m->versionReadyAfter?0xdeadfeed:m->version;}
  else{m->ownReads++;*v=4;}
 }else{assert(0);return 0;}
 return 1;
}
static int write_reg(void *p,uint64_t a,uint32_t v){
 MODEL *m=p;unsigned bank=0;
 assert(m->writes<64);m->address[m->writes]=a;m->value[m->writes++]=v;
 for(unsigned i=0;i<4;i++)if(a==ConnPinAddress[i]){
  assert(!((v^m->pin[i])&~ConnPinMask[i]));
  if(m->pinFault!=i+1)m->pin[i]=v;return 1;
 }
 if(a==CONN_CTL){
  m->ctl=v&UINT32_C(0x3fffffff);
  if((v&4) && m->ackFault!=1)m->ctl|=UINT32_C(1)<<30;
  if((v&8) && m->ackFault!=2)m->ctl|=UINT32_C(1)<<31;
 }else{
  assert(a==CONN_BUS+4 || a==CONN_BUS+8 || a==CONN_BUS+0x1c4 || a==CONN_BUS+0x1c8);
  bank=a>=CONN_BUS+0x100;
  assert(v==1 || v==2 || v==(UINT32_C(1)<<25) || v==(UINT32_C(1)<<26));
  if(a==CONN_BUS+4 || a==CONN_BUS+0x1c4){if(!m->restoreFault)m->en[bank]|=v;}
  else if(!m->clearFault)m->en[bank]&=~v;
 }
 assert((m->en[0]&~CONN_I_MASK)==m->foreign[0]);
 assert((m->en[1]&~3U)==m->foreign[1]);return 1;
}
static void delay(void *p,unsigned us){MODEL *m=p;assert(us==10 || us==50 || us==1000);m->delays++;}
static void log_stage(void *p,unsigned s,uint64_t a,uint32_t v){(void)p;(void)s;(void)a;(void)v;}
static MODEL cold(void){MODEL m={0};m.ctl=0x12;m.cfg=UINT32_C(0x0b160001);m.foreign[0]=0x80000020;m.foreign[1]=0x10000;m.en[0]=m.foreign[0]|CONN_I_MASK;m.en[1]=m.foreign[1]|3;m.version=0x02050400;
 for(unsigned i=0;i<4;i++)m.pinInitial[i]=m.pin[i]=0xaaaaaaaau;
 return m;}
static DEGAS_CONN_RESULT run(MODEL *m){DEGAS_CONN_RESULT r;DEGAS_CONN_IO io={m,read_reg,write_reg,delay,log_stage};DegasConnPowerProbe(&io,&r);return r;}
static int active_failure(void *Context,const DEGAS_CONN_RESULT *R){
 MODEL *M=Context;assert(R->Owned && R->Version==0x02050400 && R->Ownership==4);
 assert((M->ctl&0xc000001f)==0xc000000d);return 0;
}
int main(void){
 MODEL m=cold();DEGAS_CONN_RESULT r=run(&m);
 assert(r.Code==ConnOk && r.Restored && r.Owned && r.Writes==26 && r.Version==0x02050400 && r.Ownership==4);
 assert(r.PinsOwned && r.PinsRestored);
 for(unsigned i=0;i<4;i++)assert(m.pin[i]==m.pinInitial[i]);
 assert(m.ctl==0x12 && m.versionReads==1 && m.ownReads==1);
 assert(m.address[9]==CONN_BUS+0x1c8 && m.value[9]==1);
 assert(m.address[10]==CONN_BUS+8 && m.value[10]==(UINT32_C(1)<<26));
 assert(m.address[11]==CONN_BUS+0x1c8 && m.value[11]==2);
 assert(m.address[12]==CONN_BUS+8 && m.value[12]==(UINT32_C(1)<<25));
 m=cold();m.ctl=UINT32_C(0xc000000d);m.en[0]=m.foreign[0];m.en[1]=m.foreign[1];r=run(&m);
 assert(!r.Code && !r.Owned && r.Restored && !m.writes);
 m=cold();m.cfg=0;r=run(&m);assert(r.Code==ConnSpmLocked && !m.writes && !m.versionReads);
 m=cold();m.ctl=0x16;r=run(&m);assert(r.Code==ConnUnexpectedState && !m.writes);
 m=cold();m.en[0]&=~CONN_I_MASK;r=run(&m);assert(r.Code==ConnUnexpectedState && !m.writes);
 m=cold();m.version=0;r=run(&m);assert(r.Code==ConnVersionMismatch && r.Restored && !m.ownReads && m.ctl==0x12);
 m=cold();m.ackFault=1;r=run(&m);assert(r.Code==ConnAckTimeout && r.Restored && !m.versionReads);
 m=cold();m.ackFault=2;r=run(&m);assert(r.Code==ConnAckTimeout && r.Restored && !m.versionReads);
 m=cold();m.clearFault=1;r=run(&m);assert(r.Code==ConnProtectionTimeout && r.Restored && !m.versionReads);
 m=cold();m.restoreFault=1;r=run(&m);assert(r.Code==ConnRestoreFailed && !r.Restored);
 /* The snapshot must also work in the actual unexpected loader state. It
  * must never access a powered-domain ID/ownership register or write MMIO. */
 m=cold();m.ctl=0x02000012;m.en[0]=0x10;m.en[1]=0;
 m.statusOr[0]=0x01002098;m.statusOr[1]=UINT32_MAX;
 {DEGAS_CONN_IO io={&m,read_reg,write_reg,delay,log_stage};
  DegasConnAonSnapshot(&io,&r);}
 assert(!r.Code && r.Restored && !r.Owned && !r.Writes && !m.writes);
 assert(r.Before==0x02000012 && r.After==r.Before && r.Mirror[0]==0x10 && r.Mirror[1]==0);
 assert(r.Protect[0]==0x01002098 && r.Protect[1]==UINT32_MAX);
 assert(!m.versionReads && !m.ownReads);
 /* Observed cold LK EN=0 baseline, with sticky RDY values. Finish powered
  * off with all owned protection bits SET, deliberately requiring reset. */
 m=cold();m.ctl=0x02000012;m.foreign[0]=m.en[0]=0x10;
 m.foreign[1]=m.en[1]=0;m.statusOr[0]=0x01002098;m.statusOr[1]=UINT32_MAX;
 r=run(&m);
 assert(!r.Code && r.Owned && r.Restored && r.Writes==26 && r.Version==0x02050400);
 assert(m.ctl==0x02000012 && m.en[0]==(0x10|CONN_I_MASK) && m.en[1]==3);
 /* Sticky RDY is also valid for an already powered domain; mirrors remain
  * an independent prerequisite, so do not replace those checks with EN. */
 m=cold();m.ctl=UINT32_C(0xc000000d);m.en[0]=m.foreign[0];m.en[1]=m.foreign[1];
 m.statusOr[0]=CONN_I_MASK;m.statusOr[1]=UINT32_MAX;r=run(&m);
 assert(!r.Code && !r.Owned && !m.writes && m.versionReads==1);
 m=cold();m.mirrorOr[0]=UINT32_C(1)<<25;r=run(&m);
 assert(r.Code==ConnProtectionTimeout && r.Restored && !m.versionReads);
 m=cold();m.mirrorOr[1]=1;r=run(&m);
 assert(r.Code==ConnProtectionTimeout && r.Restored && !m.versionReads);
 for(unsigned i=0;i<4;i++){
  m=cold();m.pinFault=i+1;r=run(&m);
  assert(r.Code==ConnIoError && r.Restored && r.PinsRestored && !m.versionReads);
  for(unsigned j=0;j<4;j++)assert(m.pin[j]==m.pinInitial[j]);
 }
 m=cold();m.versionReadyAfter=10;r=run(&m);
 assert(!r.Code && r.Restored && m.versionReads==11 && m.ownReads==1);
 m=cold();m.versionReadyAfter=11;r=run(&m);
 assert(r.Code==ConnVersionMismatch && r.Restored && m.versionReads==11 && !m.ownReads);
 m=cold();{DEGAS_CONN_IO io={&m,read_reg,write_reg,delay,log_stage};
  DegasConnPowerProbeActive(&io,&r,active_failure,&m);}
 assert(r.Code==ConnIoError && r.Restored && r.PinsRestored && m.ctl==0x12);
 puts("PASS: CONN cold/on, exact protection order, lock/partial state, timeout, version failure, restoration, active-client failure cleanup and unrelated-bit preservation");return 0;
}
