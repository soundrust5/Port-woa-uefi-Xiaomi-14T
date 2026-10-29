#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#define STATIC static
#define VOID void
#define TRUE 1
#define FALSE 0
#define DEBUG(x) ((void)0)
typedef uint32_t UINT32;typedef size_t UINTN;typedef int BOOLEAN;
static unsigned scenario,is,commands,delay,writes;
static unsigned MmioRead32(unsigned a){switch(a-0x112b0000){case 0x30:return scenario==3?0:(scenario==4?8:9);case 0x20:return is;case 0x98:return scenario==2?1:0;case 0x38:case 0x3c:return 0;default:assert(0);return 0;}}
static void MmioWrite32(unsigned a,unsigned v){unsigned o=a-0x112b0000;assert(o==0x20||o==0x94||o==0x98||o==0x9c||o==0x90);writes++;if(o==0x20){assert(v==0x400);is&=~v;}if(o==0x90){assert(v==0x16);commands++;if(scenario!=1)is|=0x400;}}
static void MicroSecondDelay(unsigned n){delay+=n;}
STATIC BOOLEAN UfsStartLink(VOID){
 UINTN N;UINT32 Is=0,Result,Hcs=0;
 if((MmioRead32(0x112b0030)&8)==0)return FALSE;
 MmioWrite32(0x112b0020,0x400);
 if(MmioRead32(0x112b0020)&0x400)return FALSE;
 MmioWrite32(0x112b0094,0);MmioWrite32(0x112b0098,0);MmioWrite32(0x112b009c,0);
 DEBUG((DEBUG_ERROR,"DEGAS UFS LINK STARTUP begin\n"));
 MmioWrite32(0x112b0090,0x16);
 for(N=0;N<10000;N++){
  Is=MmioRead32(0x112b0020);if(Is&0x400)break;MicroSecondDelay(100);
 }
 Result=MmioRead32(0x112b0098)&0xff;
 DEBUG((DEBUG_ERROR,"DEGAS UFS LINK completion=%u result=%08x polls=%u is=%08x\n",N<10000,Result,(UINT32)N,Is));
 if(N==10000 || Result)return FALSE;
 MmioWrite32(0x112b0020,0x400);
 for(N=0;N<1000;N++){
  Hcs=MmioRead32(0x112b0030);if(Hcs&1)break;MicroSecondDelay(100);
 }
 DEBUG((DEBUG_ERROR,"DEGAS UFS LINK device_present=%u hcs=%08x pa=%08x dl=%08x\n",!!(Hcs&1),Hcs,MmioRead32(0x112b0038),MmioRead32(0x112b003c)));
 return (Hcs&1)!=0;
}

int main(void){assert(UfsStartLink());assert(commands==1&&is==0);scenario=1;commands=delay=0;assert(!UfsStartLink());assert(commands==1&&delay==1000000);scenario=2;commands=0;assert(!UfsStartLink());assert(commands==1);scenario=3;writes=0;assert(!UfsStartLink());assert(writes==0);scenario=4;delay=0;assert(!UfsStartLink());assert(delay==100000);return 0;}
