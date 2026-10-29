#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>
#define STATIC static
#define VOID void
#define FALSE 0
#define DEBUG(x) ((void)0)
typedef uint32_t UINT32;typedef uint8_t UINT8;typedef size_t UINTN;typedef int BOOLEAN;
static volatile UINT32 ram[16384];static unsigned regs[64],scenario,ring,delay;
static void MemoryFence(void){}
static unsigned MmioRead32(unsigned a){unsigned o=a-0x112b0000;if(o==0x30)return scenario==3?0:15;if(o==0x58&&ring&&scenario!=1){volatile unsigned char *u=(volatile unsigned char *)ram+4096;ram[2]=0;u[32]=0x20;u[35]=scenario==2?0:0x5a;regs[o/4]=0;}return regs[o/4];}
static void MmioWrite32(unsigned a,unsigned v){unsigned o=a-0x112b0000;assert(o==0x20||o==0x50||o==0x54||o==0x60||o==0x58);if(o==0x20)regs[o/4]&=~v;else regs[o/4]=v;if(o==0x58){assert(v==1);assert(ram[0]==0x11000000&&ram[2]==15&&ram[4]==0x44471000&&ram[6]==0x80008&&ram[7]==0);ring++;}}
static void MicroSecondDelay(unsigned n){delay+=n;}
STATIC BOOLEAN UfsNopTest(VOID){
 volatile UINT32 *Trd=ram;
 volatile UINT8 *Ucd=((volatile UINT8 *)ram+4096);
 UINTN N;UINT32 Door,Is;BOOLEAN Ok;
 if((MmioRead32(0x112b0030)&0xf)!=0xf || MmioRead32(0x112b0024)!=0 ||
    MmioRead32(0x112b0050)!=0 || MmioRead32(0x112b0054)!=0 ||
    MmioRead32(0x112b0058)!=0 || MmioRead32(0x112b0060)!=0)return FALSE;
 for(N=0;N<0x4000;N++)Trd[N]=0;
 Trd[0]=0x11000000;Trd[2]=0xf;Trd[4]=0x44471000;
 Trd[6]=0x00080008; // Response length8 and offset8, both in DWORDs.
 Ucd[3]=0x5a; // Nonzero tag to distinguish a real reply from zeroed RAM.
 MemoryFence();
 DEBUG((DEBUG_ERROR,"DEGAS UFS NOP DMA trl=44470000 ucd=44471000 bytes=65536\n"));
 MmioWrite32(0x112b0020,1);
 MmioWrite32(0x112b0050,0x44470000);MmioWrite32(0x112b0054,0);
 if(MmioRead32(0x112b0050)!=0x44470000 || MmioRead32(0x112b0054)!=0)return FALSE;
 MmioWrite32(0x112b0060,1);
 if((MmioRead32(0x112b0060)&1)==0)return FALSE;
 MemoryFence();MmioWrite32(0x112b0058,1);
 for(N=0;N<10000;N++){
  Door=MmioRead32(0x112b0058);Is=MmioRead32(0x112b0020);
  if((Door&1)==0 || (Is&0x70000))break;
  MicroSecondDelay(100);
 }
 MemoryFence();
 Ok=(Door&1)==0 && !(Is&0x70000) && (Trd[2]&0xff)==0 &&
    Ucd[32]==0x20 && Ucd[35]==0x5a && Ucd[38]==0 && Ucd[40]==0 &&
    Ucd[42]==0 && Ucd[43]==0;
 DEBUG((DEBUG_ERROR,"DEGAS UFS NOP ok=%u polls=%u door=%08x is=%08x ocs=%08x type=%02x tag=%02x response=%02x\n",Ok,(UINT32)N,Door,Is,Trd[2],Ucd[32],Ucd[35],Ucd[38]));
 // Buffer stays reserved even on timeout. Caller disables HCE before clearing base.
 MmioWrite32(0x112b0060,0);
 return Ok;
}

static void init(unsigned s){memset((void*)ram,0,sizeof ram);memset(regs,0,sizeof regs);ring=delay=0;scenario=s;}
int main(void){init(0);assert(UfsNopTest());assert(ring==1&&regs[0x60/4]==0);init(1);assert(!UfsNopTest());assert(delay==1000000&&ring==1);init(2);assert(!UfsNopTest());init(3);assert(!UfsNopTest());assert(ring==0);return 0;}
