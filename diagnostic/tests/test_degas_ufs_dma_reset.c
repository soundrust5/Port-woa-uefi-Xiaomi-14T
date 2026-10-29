#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#define STATIC static
#define CONST const
#define VOID void
#define TRUE 1
#define FALSE 0
#define DEBUG(x) ((void)0)
typedef uint8_t UINT8;typedef uint32_t UINT32;typedef uint64_t UINT64;typedef uintptr_t UINTN;typedef int BOOLEAN;
static UINT32 run;
static UINT32 MmioRead32(UINT32 a){switch(a){
case 0x112b0030:return 15;case 0x112b0024:return 0;
case 0x112b0050:return 0x44470000;case 0x112b0054:return 0;
case 0x112b0058:return 0;case 0x112b0060:return run;case 0x112b0020:return 1;
default:assert(0);return 0;}}
static void MmioWrite32(UINT32 a,UINT32 v){switch(a){
case 0x112b0060:run=v;break;
case 0x112b0058:assert(v==1);((UINT32 *)0x44470000)[2]=0;break;
case 0x112b0020:break;default:assert(0);}}
static void MemoryFence(void){}
static void MicroSecondDelay(UINT32 n){(void)n;}
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/UfsCapacity.h"
int main(void){unsigned char q[32],*p;unsigned i;UINT32 *t,*pr;
p=mmap((void *)0x44470000,65536,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
assert(p==(void *)0x44470000);memset(p,0xa5,65536);
DegasUfsReadCommand(q,0,14,3);assert(UfsProbePacket(q,57344));
t=(void *)p;pr=(void *)(p+0x1100);assert(t[2]==0 && t[3]==0 && t[5]==0);
assert(memcmp(p+0x1000,q,32)==0);for(i=0x1020;i<0x1100;i++)assert(p[i]==0);
assert(pr[0]==0x44472000 && pr[1]==0 && pr[2]==0 && pr[3]==57343);
for(i=0x1110;i<0x1120;i++)assert(p[i]==0);
for(i=0x1120;i<65536;i++)assert(p[i]==0xa5);
for(i=32;i<0x1000;i++)assert(p[i]==0xa5);
munmap(p,65536);puts("UFS descriptor reset boundaries PASS; payload untouched");}
