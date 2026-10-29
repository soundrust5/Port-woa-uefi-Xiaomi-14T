#include <assert.h>
#include <stdio.h>
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/PmicRead.h"
struct model {unsigned int fault,phase,writes,delays,command;};
static unsigned int rd(void *p,unsigned int off){
 struct model *m=p;
 if(off==0)return m->fault==1?0:1;
 if(off==0x894){assert(m->phase==1);return 0x1e;}
 assert(off==0x8a8);
 if(m->phase==0)return m->fault==2?12:0;
 if(m->phase==1)return m->fault==3?2:12;
 return m->fault==4?12:0;
}
static void wr(void *p,unsigned int off,unsigned int val){
 struct model *m=p;m->writes++;
 if(m->phase==0){assert(off==0x880);assert(val==(m->command?m->command:0xc400001e));assert(!(val&(1u<<29)));m->phase=1;}
 else{assert(m->phase==1);assert(off==0x8a4 && val==1);m->phase=2;}
}
static void delay(void *p,unsigned int us){struct model *m=p;assert(us==10);m->delays++;}
struct power_model {unsigned int reg,data,phase,bad,writes;};
static unsigned int pr(void *v,unsigned int a){
 struct power_model *m=v;
 if(a==0)return 1;
 if(a==0x8a8)return m->phase?12:0;
 assert(a==0x894);return m->reg;
}
static void pw(void *v,unsigned int a,unsigned int d){
 struct power_model *m=v;
 if(a==0x884){m->data=d;return;}
 if(a==0x8a4){assert(d==1);m->phase=0;return;}
 assert(a==0x880);
 if(d==0xc5001c87){m->phase=1;return;}
 assert(d==0xe5001c87);m->writes++;
 if(!m->bad)m->reg=m->data;
}
static void pd(void *v,unsigned int n){(void)v;assert(n==10);}
int main(void){
 for(unsigned int f=0;f<=4;f++){
  struct model m={.fault=f};DEGAS_PMIF_IO io={&m,rd,wr,delay};unsigned int v=0xdead;
  assert(DegasPmicKeysRead(&io,&v)==f);
  assert(m.writes==(f==1||f==2?0:f==3?1:2));
  assert(v==(f?0xdead:0x1e));assert(m.delays==(f>=2?1000:0));
 }
 {
  unsigned int addresses[]={0x1c87,0x1da4,0x1da5};
  for(unsigned int i=0;i<3;i++){
   struct model m={.command=0xc5000000u|addresses[i]};
   DEGAS_PMIF_IO io={&m,rd,wr,delay};unsigned int v=0xdead;
   assert(DegasPmicReadByte(&io,5,addresses[i],&v)==0 && v==0x1e);
   unsigned int writes=m.writes;
   assert(DegasPmicReadByte(&io,16,addresses[i],&v)==5);
   assert(DegasPmicReadByte(&io,5,65536,&v)==5 && m.writes==writes);
  }
 }
 {
  struct power_model m={.reg=0xa2};DEGAS_PMIF_IO io={&m,pr,pw,pd};
  assert(!DegasPmicVtpEnable(&io,1) && m.reg==0xa3 && m.writes==1);
  assert(!DegasPmicVtpEnable(&io,1) && m.writes==1);
  assert(!DegasPmicVtpEnable(&io,0) && m.reg==0xa2 && m.writes==2);
  m.bad=1;assert(DegasPmicVtpEnable(&io,1)==6 && m.reg==0xa2);
  assert(DegasPmicVtpEnable(&io,2)==5 && m.writes==3);
 }
 puts("PASS: read opcode, idle ownership, completion, bounded timeouts, no stale output");
}
