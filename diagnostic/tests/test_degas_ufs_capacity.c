#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>
#include <sys/mman.h>
#define STATIC static
#define CONST const
#define VOID void
#define TRUE 1
#define FALSE 0
#define DEBUG(x) ((void)0)
typedef uint8_t UINT8;typedef uint32_t UINT32;typedef uint64_t UINT64;typedef size_t UINTN;typedef int BOOLEAN;
static unsigned regs[64],scenario,queries,scsi,delays;
static void MemoryFence(void){}
static void MicroSecondDelay(unsigned n){delays+=n;}
static unsigned MmioRead32(unsigned a){if(a==0x112b0030)return 15;return regs[(a-0x112b0000)/4];}
static void MmioWrite32(unsigned a,unsigned v){
 unsigned o=a-0x112b0000;assert(o==0x20||o==0x60||o==0x58);
 if(o==0x20)regs[o/4]&=~v;else regs[o/4]=v;
 if(o==0x58){
  unsigned *t=(void*)0x44470000;unsigned char *q=(void*)0x44471000,*r=q+128,*d=(void*)0x44472000;
  assert(v==1&&t[4]==0x44471000&&t[6]==0x200010);
  if(scenario==4)return;
  t[2]=0;regs[o/4]=0;regs[0x20/4]|=1;r[3]=q[3];
  if(q[0]==0x16){assert(t[7]==0&&q[13]==1&&(q[12]==5||q[12]==6));queries++;r[0]=0x36;r[5]=q[5];r[12]=q[12];r[13]=1;r[23]=scenario==3?1:0;}
  else {assert(q[0]==1&&q[2]==2&&q[16]==0x9e&&q[17]==0x10&&q[29]==32);assert(t[0]==0x15000000&&t[7]==0x400001);unsigned *pr=(void*)0x44471100;assert(pr[0]==0x44472000&&pr[3]==31);scsi++;r[0]=0x21;r[2]=2;
   if(scenario==1&&scsi==1){r[6]=1;r[7]=2;r[1]=0x20;r[15]=32;r[11]=20;r[33]=18;r[34]=0x70;r[36]=6;}
   else {d[4]=3;d[5]=0xd9;d[6]=0x5f;d[7]=0xff;d[10]=scenario==2?8:16;}
  }
 }
}
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/UfsCapacity.h"
static void init(unsigned s){memset((void*)0x44470000,0,65536);memset(regs,0,sizeof regs);regs[0x50/4]=0x44470000;scenario=s;queries=scsi=delays=0;}
int main(void){assert(mmap((void*)0x44470000,65536,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==(void*)0x44470000);
 init(0);assert(UfsCapacityProbe());assert(queries==2&&scsi==1);
 init(1);assert(UfsCapacityProbe());assert(scsi==2);
 init(2);assert(!UfsCapacityProbe());
 init(3);assert(!UfsCapacityProbe());assert(queries==21&&scsi==0);
 init(4);assert(!UfsCapacityProbe());assert(delays==1000000);
 return 0;}
