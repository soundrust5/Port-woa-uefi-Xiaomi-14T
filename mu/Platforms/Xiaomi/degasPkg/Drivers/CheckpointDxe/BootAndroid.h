// One-shot early-wrapper route, consumed on the next boot before SEC/DXE.
// Stock Image and the early route must be present in the packaged boot image.
#if DEGAS_ANDROID_EFI_BOOT
#include "AndroidEfiBoot.h"
#endif
STATIC VOID AndroidRouteReport(VOID){
 volatile UINT64 *R=(volatile UINT64 *)(UINTN)0x480aef20;
 CONST UINT64 Tag=0x3154554f52414744ULL;
 if(R[0]==Tag)DEBUG((DEBUG_ERROR,"DEGAS ANDROID entry stage=%lu flag=%lx inverse=%lx el=%lx sctlr=%lx request=%lx source=%lu\n",R[1],R[2],R[3],R[4],R[5],R[6],R[7]));
 if(R[8]==Tag)DEBUG((DEBUG_ERROR,"DEGAS ANDROID last_match stage=%lu flag=%lx inverse=%lx el=%lx sctlr=%lx source=%lu\n",R[9],R[10],R[11],R[12],R[13],R[15]));
}
STATIC inline VOID AndroidRouteScreen(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop,UINTN Tick){
 volatile UINT64 *R=(volatile UINT64 *)(UINTN)0x480aef20;
 CONST UINT64 Tag=0x3154554f52414744ULL,Magic=0x313052444e414744ULL;
 CHAR8 Text[64];
 UINT32 E=R[0]==Tag?(UINT32)R[1]:9;
 UINT32 M=R[8]==Tag?(UINT32)R[9]:9;
 UINT32 S=R[0]==Tag?(UINT32)R[7]:0;
 UINT32 Q=R[6]==Magic;
 AsciiSPrint(Text,sizeof(Text),"A E %u M %u S %u Q %u",E,M,S,Q);Label(Gop,1900,Text);
 AsciiSPrint(Text,sizeof(Text),"ROUTE V9 TICK %u",(UINT32)Tick);Label(Gop,2000,Text);
}
STATIC VOID BootAndroid(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop){
#if DEGAS_ANDROID_EFI_BOOT
 AndroidEfiBoot(Gop);
#else
 volatile UINT64 *Flag=(volatile UINT64 *)(UINTN)0x480aef00;
 CONST UINT64 Magic=0x313052444e414744ULL;
 if(MmioRead32(0x40000000)!=0x14004000U){Label(Gop,1690,"ANDROID ROUTE NOT PACKAGED");return;}
 volatile UINT64 *R=(volatile UINT64 *)(UINTN)0x480aef20;
 for(UINTN I=0;I<16;I++)R[I]=0;
 R[0]=0x3154554f52414744ULL;R[1]=1;R[6]=Magic;MemoryFence();
 Flag[0]=0;MemoryFence();Flag[1]=~Magic;MemoryFence();Flag[0]=Magic;MemoryFence();
 if(Flag[0]!=Magic || Flag[1]!=~Magic){Label(Gop,1690,"ANDROID REQUEST FAILED");return;}
 DEBUG((DEBUG_ERROR,"DEGAS ANDROID one-shot request committed reset required\n"));
 Label(Gop,1690,"RESTARTING TO ANDROID");
 BootWatchdogArmTenSeconds();
 DEBUG((DEBUG_ERROR,"DEGAS ANDROID issuing software reset SWRST=1209\n"));
 // Publish last, after all output. Mask IRQs so no timer/debug writer can
 // append after this token. Reset-only watchdog continues independently.
 CONST UINT64 Request[2]={Magic,~Magic};
 BOOLEAN InterruptsEnabled=SaveAndDisableInterrupts();
 if(SerialPortWrite((UINT8 *)Request,sizeof(Request))!=sizeof(Request)){
  Flag[0]=0;Flag[1]=0;MemoryFence();SetInterruptState(InterruptsEnabled);
  Label(Gop,1690,"ANDROID REQUEST FAILED");return;
 }
 MemoryFence();BootSoftwareReset();
#endif
}
