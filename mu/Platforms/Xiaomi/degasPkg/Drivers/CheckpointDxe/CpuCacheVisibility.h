// Isolated explicit-menu probe: never used during normal Windows boot.
#include <Protocol/Cpu.h>
extern UINT8 DegasCacheProbeStart[],DegasCacheProbeEnd[],DegasCacheProbeMailbox[];
STATIC BOOLEAN CacheVisibilityWait(volatile UINT64 *Mail,UINTN Word,UINT64 Value){
 UINTN N;
 for(N=0;N<5000;N++){
  MemoryFence();if(Mail[Word]==Value)return TRUE;if(Mail[9])return FALSE;
  MicroSecondDelay(100);
 }
 return FALSE;
}
STATIC inline VOID CpuCacheVisibilityTest(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop){
 EFI_PHYSICAL_ADDRESS Code=MAX_UINT32,MailAddress=MAX_UINT32,DataAddress=MAX_UINT32;
 EFI_CPU_ARCH_PROTOCOL *Cpu=NULL;EFI_STATUS S;ARM_SMC_ARGS Smc={0};
 UINTN Size=(UINTN)DegasCacheProbeEnd-(UINTN)DegasCacheProbeStart;
 UINTN Literal=(UINTN)DegasCacheProbeMailbox-(UINTN)DegasCacheProbeStart,N;
 volatile UINT64 *Mail=NULL,*Data=NULL;UINT32 State=MAX_UINT32;BOOLEAN Started=FALSE;
 UINT64 BspSctlr=ArmReadSctlr();CHAR8 Text[96];CONST CHAR8 *Result="CACHE TEST STOPPED";
 CONST UINT64 Baseline=0x1122334455667788ULL,Dirty=0x8877665544332211ULL;
 BootWatchdogArmTenSeconds();
 if(Size<8 || Size>EFI_PAGE_SIZE || Literal>Size-8 || (Literal&7) ||
    (ArmReadMpidr()&0xff00ffffffULL)!=0 || !(BspSctlr&BIT0) || !(BspSctlr&BIT2))goto Done;
 if(CpuAffinityState(0x100)!=1)goto Done;
 Smc.Arg0=ARM_SMC_ID_PSCI_FEATURES;Smc.Arg1=ARM_SMC_ID_PSCI_CPU_OFF;ArmCallSmc(&Smc);if((UINT32)Smc.Arg0)goto Done;
 S=gBS->LocateProtocol(&gEfiCpuArchProtocolGuid,NULL,(VOID **)&Cpu);if(EFI_ERROR(S))goto Done;
 S=gBS->AllocatePages(AllocateMaxAddress,EfiBootServicesCode,1,&Code);if(EFI_ERROR(S))goto Done;
 S=gBS->AllocatePages(AllocateMaxAddress,EfiBootServicesData,1,&MailAddress);if(EFI_ERROR(S))goto Done;
 S=gBS->AllocatePages(AllocateMaxAddress,EfiBootServicesData,1,&DataAddress);if(EFI_ERROR(S))goto Done;
 Mail=(volatile UINT64 *)(UINTN)MailAddress;Data=(volatile UINT64 *)(UINTN)DataAddress;
 ZeroMem((VOID *)(UINTN)MailAddress,EFI_PAGE_SIZE);
 ZeroMem((VOID *)(UINTN)DataAddress,EFI_PAGE_SIZE);
 WriteBackInvalidateDataCacheRange((VOID *)(UINTN)MailAddress,EFI_PAGE_SIZE);
 S=Cpu->SetMemoryAttributes(Cpu,MailAddress,EFI_PAGE_SIZE,EFI_MEMORY_UC|EFI_MEMORY_XP);if(EFI_ERROR(S))goto Done;
 S=Cpu->SetMemoryAttributes(Cpu,DataAddress,EFI_PAGE_SIZE,EFI_MEMORY_WB|EFI_MEMORY_XP);if(EFI_ERROR(S))goto Done;
 Mail[8]=DataAddress;MemoryFence();
 CopyMem((VOID *)(UINTN)Code,DegasCacheProbeStart,Size);
 WriteUnaligned64((UINT64 *)(UINTN)(Code+Literal),MailAddress);
 WriteBackInvalidateDataCacheRange((VOID *)(UINTN)Code,EFI_PAGE_SIZE);
 InvalidateInstructionCacheRange((VOID *)(UINTN)Code,EFI_PAGE_SIZE);
 *Data=Baseline;WriteBackDataCacheRange((VOID *)(UINTN)DataAddress,8);MemoryFence();
 DEBUG((DEBUG_ERROR,"DEGAS CACHE VISIBILITY CPU1 code=%Lx mail_UC=%Lx data_WB=%Lx BSP_SCTLR=%Lx baseline=%Lx dirty=%Lx\n",Code,MailAddress,DataAddress,BspSctlr,Baseline,Dirty));
 ZeroMem(&Smc,sizeof(Smc));Smc.Arg0=ARM_SMC_ID_PSCI_CPU_ON_AARCH64;Smc.Arg1=0x100;Smc.Arg2=Code;ArmCallSmc(&Smc);
 if((UINT32)Smc.Arg0)goto Done;Started=TRUE;
 if(!CacheVisibilityWait(Mail,2,1))goto Done;
 if((Mail[3]&0xff00ffffffULL)!=0x100 || Mail[5]!=4 || (Mail[4]&0x1005))goto Done;
 Mail[0]=1;MemoryFence();if(!CacheVisibilityWait(Mail,1,1))goto Done;
 // Measurement window: no DEBUG, SMC or cache maintenance before phase2 ends.
 *Data=Dirty;MemoryFence();Mail[0]=2;MemoryFence();
 if(!CacheVisibilityWait(Mail,1,2))goto Done;
 // Same dirty value, only the explicit writeback differs from phase2.
 WriteBackDataCacheRange((VOID *)(UINTN)DataAddress,8);MemoryFence();
 Mail[0]=3;MemoryFence();if(!CacheVisibilityWait(Mail,1,3))goto Done;
 if(Mail[16]!=Baseline || Mail[18]!=Dirty)Result="BASELINE OR CLEAN READ MISMATCH";
 else if(Mail[17]==Baseline)Result="DIRTY HIDDEN THEN CLEAN VISIBLE";
 else if(Mail[17]==Dirty)Result="DIRTY VISIBLE - INCONCLUSIVE";
 else Result="UNEXPECTED DIRTY READ";
Done:
 if(Started){
  for(N=0;N<5000;N++){State=CpuAffinityState(0x100);if(State==1)break;MicroSecondDelay(100);}
  if(State!=1 || (Mail && Mail[9]))Result="AP ERROR OR OFF FAILED - RESET ONLY";
 }
 DEBUG((DEBUG_ERROR,"DEGAS CACHE VISIBILITY result=%a BSP_SCTLR=%Lx AP_started=%u final_state=%08x command=%Lu done=%Lu status=%Lu AP_mpidr=%Lx AP_SCTLR=%Lx AP_EL=%Lu baseline_read=%Lx dirty_read=%Lx clean_read=%Lx RESET REQUIRED\n",Result,BspSctlr,Started,State,Mail?Mail[0]:0,Mail?Mail[1]:0,Mail?Mail[9]:0,Mail?Mail[3]:0,Mail?Mail[4]:0,Mail?Mail[5]:0,Mail?Mail[16]:0,Mail?Mail[17]:0,Mail?Mail[18]:0));
 if(Mail){
  AsciiSPrint(Text,sizeof(Text),"BASELINE %016Lx",Mail[16]);Label(Gop,1690,Text);
  AsciiSPrint(Text,sizeof(Text),"DIRTY    %016Lx",Mail[17]);Label(Gop,1770,Text);
  AsciiSPrint(Text,sizeof(Text),"CLEAN    %016Lx",Mail[18]);Label(Gop,1850,Text);
 }
 Label(Gop,1930,Result);
 // No allocations are released: an AP timeout must never permit OS boot.
 BootWatchdogArmTenSeconds();if(Started && State!=1)CpuDeadLoop();
}
