/* Independent instruction decoder + injected controller model. NO hardware.
 * gcc -std=c11 -O2 -Wall -Wextra -Werror test_degas_gce_one_frame.c -o test
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../include/degas_gce_one_frame.h"
static const uint32_t addresses[]={
 0x1400d190,0x1400d190,0x1400d008,0x1400d014,0x1400d100,0x1400d104,
 0x14201008,0x14201020,0x14601008,0x14601020,
 0x14001008,0x14001020,0x14401008,0x14401020,
 0x1400dd00,0x1400d060,0x1400d000,0x1400d000
};
typedef struct {
 uint32_t control[32][32],slot,cg0,cg1,gctl,dsi_status,mode;
 uint32_t pixel_regs[18],display_writes[18],display_values[18];
 uint64_t words[DEGAS_GCE_MAX_WORDS];size_t count;
 unsigned writes,display_count,us,fault,published,thread;
 bool eoc,pass_jump;
} Model;
enum {RESET_FAIL=1,STALL=2,ERROR_IRQ=3,STOP_FAIL=4,PUBLISH_FAIL=5};
static int reg_index(uint32_t address){
 for(unsigned i=0;i<18;i++)if(addresses[i]==address)return (int)i;
 return -1;
}
static uint32_t reg_read(Model *m,uint32_t address){
 if(address==0x1400d00c)return m->dsi_status;
 if(address==0x1400d014)return m->mode;
 int i=reg_index(address);assert(i>=0);return m->pixel_regs[i];
}
static void reg_write(Model *m,uint32_t address,uint32_t value){
 unsigned n=m->display_count;assert(n<18&&addresses[n]==address);
 m->display_writes[n]=address;m->display_values[n]=value;m->display_count++;
 int index=reg_index(address);assert(index>=0);m->pixel_regs[index]=value;
 if(address==0x1400d000&&(value&1))m->dsi_status=0x10;
}
/* Decode raw field positions independently; do not call encoder helpers. */
static void execute(Model *m){
 uint32_t mask_inverse=0,global_r1=0,spr0=0;unsigned polls=0,busy_polls=0;
 for(size_t i=0;i<m->count;i++){
  uint64_t w=m->words[i];unsigned op=(unsigned)(w>>56),a=(unsigned)((w>>32)&0xffff);
  unsigned sop=(unsigned)((w>>48)&31),atype=(unsigned)((w>>55)&1);
  uint32_t value=(uint32_t)w;
  assert(((w>>53)&3)==0); /* no B/C register operand encodings */
  if(op==2){
   if(atype){assert(a==0&&sop==1);global_r1=value;}
   else {assert(a==0&&sop==0);mask_inverse=value;}
  }else if(op==0xa0){assert(atype&&sop==0&&a==0);spr0=value;
  }else if(op==0x90||op==0x91){
   assert(!atype&&sop==0&&(a&3)==2);
   uint32_t address=(spr0<<16)|(a&~3u),old=reg_read(m,address);
   reg_write(m,address,op==0x91?(old&mask_inverse)|(value&~mask_inverse):value);
  }else if(op==8){
   assert(atype&&sop==1&&a==1&&global_r1==0x1400d00c);
   assert(mask_inverse==0x7fffffff);
   if(value==0x80000000){assert(m->display_count==18);busy_polls++;m->dsi_status=0x80000010;}
   else {assert(value==0);m->dsi_status&=~0x80000000u;}
   assert((m->dsi_status&~mask_inverse)==value);polls++;
  }else if(op==0x40){assert(w==UINT64_C(0x4000000000000000));m->eoc=true;
  }else if(op==0x10){assert(w==UINT64_C(0x1000000000000001)&&i+1==m->count);m->pass_jump=true;
  }else assert(0);
 }
 assert(polls==3 && busy_polls==0 && m->display_count==18 && m->eoc && m->pass_jump);
 m->control[m->thread][0x20/4]=m->control[m->thread][0x24/4];
}
static uint32_t read32(void *context,uint32_t address){
 Model *m=context;
 if(address==0x1e800100)return m->cg0;
 if(address==0x1e800110)return m->cg1;
 if(address==DEGAS_GCE_BASE+0x30)return m->slot;
 if(address==DEGAS_GCE_BASE+0x48)return m->gctl;
 if(address>=DEGAS_GCE_BASE+0x100&&address<DEGAS_GCE_BASE+0x1100){
  unsigned rel=address-DEGAS_GCE_BASE-0x100;return m->control[rel/0x80][(rel%0x80)/4];
 }
 return reg_read(m,address);
}
static void write32(void *context,uint32_t address,uint32_t value){
 Model *m=context;m->writes++;
 if(address==DEGAS_GCE_BASE+0x30){assert(value==0x3200);m->slot=value;return;}
 assert(address>=DEGAS_GCE_BASE+0x100&&address<DEGAS_GCE_BASE+0x1100);
 unsigned rel=address-DEGAS_GCE_BASE-0x100,t=rel/0x80,off=rel%0x80;assert(t==m->thread);
 uint32_t *r=m->control[t];r[off/4]=value;
 switch(off){
 case 0:assert(value==1);r[0]=m->fault==RESET_FAIL?1:0;break;
 case 4:
  if(value){assert(value==1&&m->published);
   if(m->fault==ERROR_IRQ)r[0x10/4]=0x10;
   else if(m->fault!=STALL)execute(m);
  }break;
 case 8:assert(value==1);r[0xc/4]=m->fault==STOP_FAIL?0:2;break;
 case 0x14:assert(value==0);break;
 case 0x20:case 0x24:case 0x40:case 0x50:break;
 default:assert(0);
 }
}
static void delay_us(void *context,unsigned us){Model *m=context;m->us+=us;}
static bool publish(void *context,const void *data,size_t bytes,uint64_t dma){
 Model *m=context;assert(dma==0x43000000&&bytes<=sizeof m->words&&!(bytes&7));
 if(m->fault==PUBLISH_FAIL)return false;
 memcpy(m->words,data,bytes);m->count=bytes/8;m->published++;return true;
}
static void init(Model *m,unsigned fault,unsigned thread){
 memset(m,0,sizeof *m);m->fault=fault;m->thread=thread;
 m->gctl=0x20002;
 m->pixel_regs[0]=0xa0;m->pixel_regs[5]=0x8888;m->pixel_regs[16]=0x20;
}
int main(void){
 struct {uint64_t before;DegasGcePacket p;uint64_t after;} box={.before=1,.after=2};
 assert(DegasGceBuildOneFrame(&box.p)&&box.before==1&&box.after==2);
 assert(box.p.count==53); /* three polls +18 writes/RMWs + EOC/pass */
 assert(box.p.word[0]==UINT64_C(0x020000007fffffff));
 assert(box.p.word[1]==UINT64_C(0x028100001400d00c));
 assert(box.p.word[2]==UINT64_C(0x0881000100000000));
 assert(box.p.word[3]==UINT64_C(0xa080000000001400));
 assert(box.p.word[4]==UINT64_C(0x02000000fffffffd));
 assert(box.p.word[5]==UINT64_C(0x9100d19200000002));
 uint32_t encoded;assert(DegasGceAddress(0x43000000,&encoded)&&encoded==0x10600000);
 assert(!DegasGceAddress(0x43000001,&encoded)&&!DegasGceAddress(UINT64_MAX,&encoded));
 assert(DegasGceAddress(UINT64_C(0x3fffffff8),&encoded)&&encoded==0x87ffffff);
 assert(!DegasGceAddress(UINT64_C(0x400000000),&encoded));
 Model m;DegasGceIo io={&m,read32,write32,delay_us,publish};
 DegasGceConfig cfg={DEGAS_GCE_PROOFS,0,0x43000000,100};DegasGceResult result;
 for(unsigned thread=0;thread<7;thread+=6){
  cfg.thread=thread;init(&m,0,thread);result=DegasGceRunOneFrame(&io,&cfg,&box.p);
  assert(result.started&&result.completed&&result.quiesced&&!result.irq&&result.dsi_status==0x10);
  assert(m.display_values[0]==0xa2&&m.display_values[1]==0xa6&&m.display_values[5]==0x8889);
  assert(m.display_values[16]==0x20&&m.display_values[17]==0x21);
  assert(result.pc==0x10600000+(box.p.count-1)&&!m.control[thread][1]);
 }
 cfg.thread=0;
 for(unsigned bit=0;bit<5;bit++){
  init(&m,0,0);cfg.proofs=DEGAS_GCE_PROOFS^(1u<<bit);
  result=DegasGceRunOneFrame(&io,&cfg,&box.p);assert(!result.started&&!m.writes&&!m.published);
 }cfg.proofs=DEGAS_GCE_PROOFS;
 for(unsigned guard=0;guard<7;guard++){
  init(&m,0,0);
  if(guard==0)m.cg0=1;
  if(guard==1)m.cg1=1u<<17;
  if(guard==2)m.control[31][1]=1;
  if(guard==3)m.mode=1;
  if(guard==4)m.control[0][0x14/4]=1;
  if(guard==5)m.dsi_status=0x80000000;
  if(guard==6)m.gctl=0;
  result=DegasGceRunOneFrame(&io,&cfg,&box.p);assert(!result.started&&!m.writes&&!m.published);
 }
 for(unsigned forbidden=7;forbidden<32;forbidden++){
  init(&m,0,0);cfg.thread=forbidden;
  result=DegasGceRunOneFrame(&io,&cfg,&box.p);assert(!result.started&&!m.writes&&!m.published);
 }
 cfg.thread=0;
 for(unsigned fault=1;fault<=5;fault++){
  init(&m,fault,0);result=DegasGceRunOneFrame(&io,&cfg,&box.p);
  if(fault==RESET_FAIL||fault==PUBLISH_FAIL)assert(!result.started&&!m.display_count);
  if(fault==STALL)assert(result.started&&!result.completed&&result.quiesced&&m.us<=1100);
  if(fault==ERROR_IRQ)assert(result.started&&!result.completed&&result.irq==0x10&&result.quiesced);
  if(fault==STOP_FAIL)assert(result.completed&&!result.quiesced);
 }
 init(&m,0,0);box.p.word[1]^=1;
 result=DegasGceRunOneFrame(&io,&cfg,&box.p);assert(!result.started&&!m.writes);
 puts("PASS: GCE exact encoder golden words, independent decoder/18 allowed display writes, no IRQ/loop, aperture+shift, thread0/6, reject7..31 including secure8/9, guards and bounded faults; NO HARDWARE");
 return 0;
}
