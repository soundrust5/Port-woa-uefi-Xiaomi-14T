#include <stdint.h>
#include <assert.h>
#include <stddef.h>
#include <string.h>
#define STATIC static
#define VOID void
#define TRUE 1
#define FALSE 0
#define DEBUG(x) ((void)0)
typedef uint32_t UINT32;
typedef size_t UINTN;
typedef int BOOLEAN;
typedef void EFI_GRAPHICS_OUTPUT_PROTOCOL;
static unsigned regs[0x3000/4], writes, delays, scenario;
static const char *label;
static unsigned MmioRead32(unsigned a){unsigned o=a-0x112b0000; if(o==0x30)return scenario==1?0:8;return regs[o/4];}
static void MmioWrite32(unsigned a,unsigned v){unsigned o=a-0x112b0000;assert(o==0x140||o==0x34);writes++;if(o==0x34 && !v && scenario==2)return;regs[o/4]=v;}
static void MicroSecondDelay(unsigned n){delays+=n;}
static void Label(void *g,unsigned y,const char *s){label=s;}
STATIC VOID UfsEnableTest(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop){
 UINT32 Xo, Hce=0, Hcs=0; UINTN N; BOOLEAN Ready=FALSE,Stopped=FALSE;
 if(MmioRead32(0x112b0008)!=0x400 || MmioRead32(0x112b2240)!=0x01440000 ||
    (MmioRead32(0x112b0144)&3)!=3 || MmioRead32(0x112b0034)!=0 ||
    MmioRead32(0x112b0024)!=0 || MmioRead32(0x112b0058)!=0 ||
    MmioRead32(0x112b0060)!=0 || MmioRead32(0x112b0078)!=0 ||
    MmioRead32(0x112b0080)!=0){
  DEBUG((DEBUG_ERROR,"DEGAS UFS ENABLE guard failed\n"));
  Label(Gop,1690,"UFS PRECONDITION FAILED");return;
 }
 Xo=MmioRead32(0x112b0140);
 DEBUG((DEBUG_ERROR,"DEGAS UFS ENABLE begin xo=%08x ref=%08x\n",Xo,MmioRead32(0x112b0144)));
 // MediaTek HCE PRE_CHANGE: CLK_CG bypasses abnormal ERR_CHK signal.
 MmioWrite32(0x112b0140,Xo|0x80);
 if(MmioRead32(0x112b0140)!=(Xo|0x80)){
  DEBUG((DEBUG_ERROR,"DEGAS UFS ENABLE XO readback failed\n"));
  Label(Gop,1690,"UFS CLOCK CHECK FAILED");return;
 }
 MmioWrite32(0x112b0034,1);MicroSecondDelay(600);
 for(N=0;N<1000;N++){
  Hce=MmioRead32(0x112b0034);Hcs=MmioRead32(0x112b0030);
  if((Hce&1) && (Hcs&8)){Ready=TRUE;break;}
  MicroSecondDelay(100);
 }
 DEBUG((DEBUG_ERROR,"DEGAS UFS ENABLE ready=%u hce=%08x hcs=%08x polls=%u is=%08x\n",Ready,Hce,Hcs,(UINT32)N,MmioRead32(0x112b0020)));
 MmioWrite32(0x112b0034,0);
 for(N=0;N<1000;N++){
  if((MmioRead32(0x112b0034)&1)==0){Stopped=TRUE;break;}
  MicroSecondDelay(100);
 }
 if(Stopped)MmioWrite32(0x112b0140,Xo);
 DEBUG((DEBUG_ERROR,"DEGAS UFS ENABLE cleanup=%u xo=%08x NO DMA NO UIC\n",Stopped,MmioRead32(0x112b0140)));
 Label(Gop,1690,!Stopped?"UFS STOP FAILED RESET REQUIRED":Ready?"UFS COMMAND INTERFACE READY":"UFS ENABLE TIMEOUT");
}

static void init(unsigned s){memset(regs,0,sizeof regs);regs[8/4]=0x400;regs[0x2240/4]=0x01440000;regs[0x144/4]=3;regs[0x140/4]=0x102;writes=delays=0;scenario=s;}
int main(void){
init(0);UfsEnableTest(0);assert(!strcmp(label,"UFS COMMAND INTERFACE READY"));assert(writes==4&&regs[0x34/4]==0&&regs[0x140/4]==0x102);
init(1);UfsEnableTest(0);assert(!strcmp(label,"UFS ENABLE TIMEOUT"));assert(delays==100600&&writes==4);
init(2);UfsEnableTest(0);assert(!strcmp(label,"UFS STOP FAILED RESET REQUIRED"));assert(writes==3&&regs[0x140/4]==0x182);
init(0);regs[0x144/4]=0;UfsEnableTest(0);assert(writes==0);
return 0;}
