// One-shot early-wrapper route, consumed on the next boot before SEC/DXE.
// Stock Image and the early route must be present in the packaged boot image.
STATIC VOID BootAndroid(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop){
 volatile UINT64 *Flag=(volatile UINT64 *)(UINTN)0x480aef00;
 CONST UINT64 Magic=0x313052444e414744ULL;
 if(MmioRead32(0x40000000)!=0x14004000U){Label(Gop,1690,"ANDROID ROUTE NOT PACKAGED");return;}
 Flag[0]=0;MemoryFence();Flag[1]=~Magic;MemoryFence();Flag[0]=Magic;MemoryFence();
 if(Flag[0]!=Magic || Flag[1]!=~Magic){Label(Gop,1690,"ANDROID REQUEST FAILED");return;}
 DEBUG((DEBUG_ERROR,"DEGAS ANDROID one-shot request committed reset required\n"));
 Label(Gop,1690,"RESTARTING TO ANDROID");
 BootWatchdogArmTenSeconds();CpuDeadLoop();
}
