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
static unsigned MmioRead32(unsigned a){unsigned o=a-0x112b0000;if(o==0x30)return 15;if(o==0x58&&ring&&scenario!=1){volatile unsigned char *r=(volatile unsigned char *)ram+4096+128;ram[2]=0;r[0]=0x36;r[3]=scenario==2?0:0x5b;r[5]=1;r[12]=1;r[11]=r[19]=89;r[32]=89;if(scenario==3)r[10]=1;if(scenario==4)r[32]=255;regs[o/4]=0;}return regs[o/4];}
static void MmioWrite32(unsigned a,unsigned v){unsigned o=a-0x112b0000;assert(o==0x20||o==0x60||o==0x58);if(o==0x20)regs[o/4]&=~v;else regs[o/4]=v;if(o==0x58){volatile unsigned char *u=(volatile unsigned char *)ram+4096;assert(v==1&&u[0]==0x16&&u[5]==1&&u[12]==1&&u[13]==0&&u[19]==255);assert(ram[6]==0x200048&&ram[7]==0);ring++;}}
static void MicroSecondDelay(unsigned n){delay+=n;}
STATIC BOOLEAN UfsDeviceDescriptor(VOID){
 volatile UINT32 *Trd=ram;
 volatile UINT8 *Ucd=((volatile UINT8 *)ram+4096);
 volatile UINT8 *R=Ucd+128,*D=R+32;
 UINTN N;UINT32 Door=1,Is=0,Len,TsfLen;BOOLEAN Ok;
 if((MmioRead32(0x112b0030)&0xf)!=0xf || MmioRead32(0x112b0024)!=0 ||
    MmioRead32(0x112b0050)!=0x44470000 || MmioRead32(0x112b0054)!=0 ||
    MmioRead32(0x112b0058)!=0 || MmioRead32(0x112b0060)!=0)return FALSE;
 for(N=0;N<0x4000;N++)Trd[N]=0;
 Trd[0]=0x11000000;Trd[2]=0xf;Trd[4]=0x44471000;
 Trd[6]=0x00200048; // Response offset128, capacity288 bytes; no PRDT.
 Ucd[0]=0x16;Ucd[3]=0x5b;Ucd[5]=1;Ucd[12]=1;Ucd[19]=255;
 MemoryFence();MmioWrite32(0x112b0020,1);
 MmioWrite32(0x112b0060,1);
 if((MmioRead32(0x112b0060)&1)==0)return FALSE;
 DEBUG((DEBUG_ERROR,"DEGAS UFS QUERY READ DEVICE DESCRIPTOR begin max=255\n"));
 MemoryFence();MmioWrite32(0x112b0058,1);
 for(N=0;N<10000;N++){
  Door=MmioRead32(0x112b0058);Is=MmioRead32(0x112b0020);
  if((Door&1)==0 || (Is&0x70000))break;MicroSecondDelay(100);
 }
 MemoryFence();Len=((UINT32)R[10]<<8)|R[11];TsfLen=((UINT32)R[18]<<8)|R[19];
 Ok=(Door&1)==0 && !(Is&0x70000) && (Trd[2]&0xff)==0 &&
    R[0]==0x36 && R[3]==0x5b && R[5]==1 && R[6]==0 && R[8]==0 &&
    R[12]==1 && R[13]==0 && R[14]==0 && R[15]==0 && Len>=26 &&
    Len<=255 && TsfLen==Len && D[0]>=26 && D[0]<=Len && D[1]==0;
 DEBUG((DEBUG_ERROR,"DEGAS UFS QUERY ok=%u polls=%u door=%08x is=%08x ocs=%08x type=%02x tag=%02x result=%02x len=%u tsflen=%u\n",Ok,(UINT32)N,Door,Is,Trd[2],R[0],R[3],R[6],Len,TsfLen));
 if(Ok){
  DEBUG((DEBUG_ERROR,"DEGAS UFS DEVICE bytes=%u luns=%u wluns=%u spec=%02x%02x manufacturer=%02x%02x\n",D[0],D[6],D[7],D[16],D[17],D[24],D[25]));
  for(N=0;N<D[0];N++)DEBUG((DEBUG_ERROR,"DEGAS UFS DESC byte=%u value=%02x\n",(UINT32)N,D[N]));
 }
 MmioWrite32(0x112b0060,0);return Ok;
}

static void init(unsigned s){memset((void*)ram,0,sizeof ram);memset(regs,0,sizeof regs);regs[0x50/4]=0x44470000;ring=delay=0;scenario=s;}
int main(void){init(0);assert(UfsDeviceDescriptor());assert(ring==1&&regs[0x60/4]==0);init(1);assert(!UfsDeviceDescriptor());assert(delay==1000000);init(2);assert(!UfsDeviceDescriptor());init(3);assert(!UfsDeviceDescriptor());init(4);assert(!UfsDeviceDescriptor());return 0;}
