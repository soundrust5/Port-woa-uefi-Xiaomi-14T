#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>
#include <sys/mman.h>
#include <stdio.h>
#define STATIC static
#define CONST const
#define VOID void
#define TRUE 1
#define FALSE 0
#define DEBUG(x) ((void)0)
typedef uint8_t UINT8;typedef uint32_t UINT32;typedef uint64_t UINT64;typedef size_t UINTN;typedef int BOOLEAN;
static unsigned regs[64],scenario,queries,scsi,delays;
static unsigned char baseline[65536];
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
  assert(q[0]==1&&q[2]==2&&q[16]==0x28&&q[24]==1&&q[21]<16);
  assert(t[0]==0x15000000&&t[7]==0x400001);unsigned *pr=(void*)0x44471100;
  assert(pr[0]==0x44472000&&pr[3]==4095);scsi++;r[0]=0x21;r[2]=2;
  memcpy(d,baseline+(unsigned)q[21]*4096,4096);
  if(scenario==1&&q[21]==7)d[3]^=1;
  if(scenario==2)r[3]^=1;

 }
}
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/UfsCapacity.h"
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/UfsReadProbe.h"
static void init(unsigned s){memset((void*)0x44470000,0,65536);memset(regs,0,sizeof regs);regs[0x50/4]=0x44470000;scenario=s;queries=scsi=delays=0;}
int main(void){assert(mmap((void*)0x44470000,65536,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==(void*)0x44470000);
 FILE *f=fopen("evidence/raw/2026-10-25-degas-ufs-read-baseline/sdc-first64k.bin","rb");assert(f&&fread(baseline,1,65536,f)==65536);fclose(f);
 init(0);assert(UfsReadProbe());assert(scsi==16);
 init(1);assert(!UfsReadProbe());assert(scsi==8);
 init(2);assert(!UfsReadProbe());assert(scsi==1);
 init(0);unsigned char q[32]={0};q[0]=1;q[1]=0x40;q[2]=2;q[14]=0x10;q[16]=0x28;q[24]=1;q[18]=3;q[19]=0xd9;q[20]=0x60;assert(!UfsProbePacket(q,4096));assert(scsi==0);
 q[18]=q[19]=q[20]=0;q[16]=0x2a;assert(!UfsProbePacket(q,4096));assert(scsi==0);
 return 0;}
