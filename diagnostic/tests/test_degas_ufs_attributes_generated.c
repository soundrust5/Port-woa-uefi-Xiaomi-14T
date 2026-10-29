#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#define STATIC static
#define CONST const
#define VOID void
#define TRUE 1
#define FALSE 0
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define DEBUG(x) ((void)0)
typedef uint16_t UINT16;typedef uint32_t UINT32;typedef size_t UINTN;typedef int BOOLEAN;
static unsigned scenario,is,commands,delay,writes;
static unsigned MmioRead32(unsigned a){switch(a-0x112b0000){case 0x30:return scenario==3?0:8;case 0x20:return is;case 0x98:return scenario==2?1:0;case 0x9c:return 0x180;default:assert(0);return 0;}}
static void MmioWrite32(unsigned a,unsigned v){unsigned o=a-0x112b0000;assert(o==0x20||o==0x94||o==0x98||o==0x9c||o==0x90);writes++;if(o==0x20){assert(v==0x400);is&=~v;}if(o==0x90){assert(v==1);commands++;if(scenario!=1)is|=0x400;}}
static void MicroSecondDelay(unsigned n){delay+=n;}
STATIC BOOLEAN UfsReadAttributes(VOID){
 STATIC CONST UINT16 Attrs[]={0x15a9,0x155e,0xd0a6,0xd0a8}; UINTN A,N;
 UINT32 Is,Result,Value;
 for(A=0;A<ARRAY_SIZE(Attrs);A++){
  if((MmioRead32(0x112b0030)&8)==0)return FALSE;
  MmioWrite32(0x112b0020,0x400); // W1C only the stale UIC completion bit.
  if(MmioRead32(0x112b0020)&0x400)return FALSE;
  MmioWrite32(0x112b0094,(UINT32)Attrs[A]<<16);
  MmioWrite32(0x112b0098,0);MmioWrite32(0x112b009c,0);
  DEBUG((DEBUG_ERROR,"DEGAS UFS DME GET begin attr=%04x\n",Attrs[A]));
  MmioWrite32(0x112b0090,1);
  for(N=0;N<1000;N++){
   Is=MmioRead32(0x112b0020);if(Is&0x400)break;MicroSecondDelay(100);
  }
  if(N==1000){DEBUG((DEBUG_ERROR,"DEGAS UFS DME GET TIMEOUT attr=%04x is=%08x\n",Attrs[A],Is));return FALSE;}
  Result=MmioRead32(0x112b0098)&0xff;Value=MmioRead32(0x112b009c);
  DEBUG((DEBUG_ERROR,"DEGAS UFS DME GET attr=%04x result=%08x value=%08x polls=%u\n",Attrs[A],Result,Value,(UINT32)N));
  MmioWrite32(0x112b0020,0x400);
  if(Result)return FALSE;
 }
 return TRUE;
}

int main(void){assert(UfsReadAttributes());assert(commands==4&&is==0);scenario=1;commands=delay=0;assert(!UfsReadAttributes());assert(commands==1&&delay==100000);scenario=2;commands=0;assert(!UfsReadAttributes());assert(commands==1);scenario=3;writes=0;assert(!UfsReadAttributes());assert(writes==0);return 0;}
