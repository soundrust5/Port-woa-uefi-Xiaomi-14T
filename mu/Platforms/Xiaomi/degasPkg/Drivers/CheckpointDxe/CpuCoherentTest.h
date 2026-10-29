#include "CpuObserverMap.h"
extern UINT8 DegasCoherentProbeStart[],DegasCoherentProbeEnd[],DegasCoherentProbeMailbox[];
STATIC VOID CpuCoherentTest(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop){
 EFI_PHYSICAL_ADDRESS Code=MAX_UINT32,MailPa=MAX_UINT32,DataPa=MAX_UINT32,Tables=MAX_UINT32;
 EFI_CPU_ARCH_PROTOCOL *Cpu=NULL;EFI_STATUS S;ARM_SMC_ARGS Smc={0};DEGAS_OBSERVER_MAP Map;
 UINTN Size=(UINTN)DegasCoherentProbeEnd-(UINTN)DegasCoherentProbeStart;
 UINTN Literal=(UINTN)DegasCoherentProbeMailbox-(UINTN)DegasCoherentProbeStart,N;
 volatile UINT64 *Mail=NULL,*Data=NULL;BOOLEAN Started=FALSE;UINT32 State=MAX_UINT32;
 CONST UINT64 A=0x1122334455667788ULL,B=0x8877665544332211ULL;
 CONST CHAR8 *Result="COHERENT TEST STOPPED";
 BootWatchdogArmTenSeconds();
 if(Size>4096 || Literal>Size-8 || (Literal&7) || (ArmReadMpidr()&0xff00ffffffULL) ||
    (ArmReadSctlr()&5)!=5 || CpuAffinityState(0x100)!=1)goto Done;
 S=gBS->LocateProtocol(&gEfiCpuArchProtocolGuid,NULL,(VOID **)&Cpu);if(EFI_ERROR(S))goto Done;
 S=gBS->AllocatePages(AllocateMaxAddress,EfiACPIMemoryNVS,1,&Code);if(EFI_ERROR(S))goto Done;
 S=gBS->AllocatePages(AllocateMaxAddress,EfiACPIMemoryNVS,1,&MailPa);if(EFI_ERROR(S))goto Done;
 S=gBS->AllocatePages(AllocateMaxAddress,EfiACPIMemoryNVS,1,&DataPa);if(EFI_ERROR(S))goto Done;
 S=gBS->AllocatePages(AllocateMaxAddress,EfiACPIMemoryNVS,8,&Tables);if(EFI_ERROR(S))goto Done;
 ZeroMem((VOID *)(UINTN)Tables,8*4096);ZeroMem((VOID *)(UINTN)MailPa,4096);
 Mail=(volatile UINT64 *)(UINTN)MailPa;Data=(volatile UINT64 *)(UINTN)DataPa;
 if(!DegasCpuObserverBuild((VOID *)(UINTN)Tables,Tables,8,Code,MailPa,DataPa,&Map))goto Done;
 CopyMem((VOID *)(UINTN)Code,DegasCoherentProbeStart,Size);
 WriteUnaligned64((UINT64 *)(UINTN)(Code+Literal),MailPa);
 WriteBackInvalidateDataCacheRange((VOID *)(UINTN)Code,4096);InvalidateInstructionCacheRange((VOID *)(UINTN)Code,4096);
 WriteBackDataCacheRange((VOID *)(UINTN)Tables,8*4096);
 WriteBackInvalidateDataCacheRange((VOID *)(UINTN)MailPa,4096);
 S=Cpu->SetMemoryAttributes(Cpu,Code,4096,EFI_MEMORY_WC);if(EFI_ERROR(S))goto Done;
 S=Cpu->SetMemoryAttributes(Cpu,MailPa,4096,EFI_MEMORY_UC|EFI_MEMORY_XP);if(EFI_ERROR(S))goto Done;
 S=Cpu->SetMemoryAttributes(Cpu,DataPa,4096,EFI_MEMORY_WB|EFI_MEMORY_XP);if(EFI_ERROR(S))goto Done;
 Mail[8]=DataPa;Mail[10]=Map.Root;Mail[11]=Map.Tcr;Mail[12]=Map.Mair;MemoryFence();
 *Data=A;WriteBackDataCacheRange((VOID *)(UINTN)DataPa,8);MemoryFence();
 DEBUG((DEBUG_ERROR,"DEGAS COHERENT begin code=%Lx mail=%Lx data=%Lx tables=%Lx count=%u tcr=%Lx mair=%Lx\n",Code,MailPa,DataPa,Tables,(UINT32)Map.TablesUsed,Map.Tcr,Map.Mair));
 Smc.Arg0=ARM_SMC_ID_PSCI_CPU_ON_AARCH64;Smc.Arg1=0x100;Smc.Arg2=Code;ArmCallSmc(&Smc);
 if((UINT32)Smc.Arg0)goto Done;Started=TRUE;
 if(!CacheVisibilityWait(Mail,2,1))goto Done;
 if((Mail[3]&0xff00ffffffULL)!=0x100 || Mail[5]!=4 || (Mail[4]&0x1005))goto Done;
 Mail[0]=1;MemoryFence();if(!CacheVisibilityWait(Mail,1,1))goto Done;
 // No logging/SMC/clean before the coherent sample. AP CIVAC follows its read.
 *Data=B;MemoryFence();Mail[0]=2;MemoryFence();if(!CacheVisibilityWait(Mail,1,2))goto Done;
 Mail[0]=3;MemoryFence();if(!CacheVisibilityWait(Mail,1,3))goto Done;
 WriteBackDataCacheRange((VOID *)(UINTN)DataPa,8);MemoryFence();
 Mail[0]=4;MemoryFence();if(!CacheVisibilityWait(Mail,1,4))goto Done;
 if(Mail[16]==A && Mail[18]==B && Mail[19]==B && Mail[22]==4)
  Result=Mail[17]==A?"COHERENT READ VERIFIED":"WB READ PASS CONTROL INCONCLUSIVE";
 else Result="COHERENT READ MISMATCH";
Done:
 if(Started){for(N=0;N<5000;N++){State=CpuAffinityState(0x100);if(State==1)break;MicroSecondDelay(100);}
  if(State!=1 || Mail[9])Result="AP ERROR RESET REQUIRED";}
 DEBUG((DEBUG_ERROR,"DEGAS COHERENT result=%a started=%u off=%x status=%Lu phase=%Lu transition=%Lu\n",Result,Started,State,Mail?Mail[9]:0,Mail?Mail[1]:0,Mail?Mail[22]:0));
 DEBUG((DEBUG_ERROR,"DEGAS COHERENT samples=%Lx/%Lx/%Lx/%Lx enabled_sctlr=%Lx\n",Mail?Mail[16]:0,Mail?Mail[17]:0,Mail?Mail[18]:0,Mail?Mail[19]:0,Mail?Mail[23]:0));
 Label(Gop,1850,Result);BootWatchdogArmTenSeconds();if(Started && State!=1)CpuDeadLoop();
}
