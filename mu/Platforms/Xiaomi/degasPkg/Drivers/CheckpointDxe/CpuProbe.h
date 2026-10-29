// Explicit menu-only AP execution test. Always return to reset, never boot OS.
#include <IndustryStandard/ArmStdSmc.h>
#include <Protocol/Cpu.h>
extern UINT8 DegasApProbeStart[],DegasApProbeEnd[],DegasApProbeMailbox[];
extern UINTN EFIAPI DegasCpuOnDirect(UINT64 Target,UINT64 Entry,UINT64 Context,VOID *InputRecord);
STATIC UINT32 CpuAffinityState(UINT64 Mpidr){
 ARM_SMC_ARGS S={0};S.Arg0=ARM_SMC_ID_PSCI_AFFINITY_INFO_AARCH64;S.Arg1=Mpidr;ArmCallSmc(&S);return (UINT32)S.Arg0;
}
STATIC inline VOID CpuExecutionTest(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop){
 EFI_PHYSICAL_ADDRESS Code=0xffffffff,Mail=0xffffffff;EFI_STATUS S;
 UINTN Size=(UINTN)DegasApProbeEnd-(UINTN)DegasApProbeStart,I,N,Method;UINT32 Passed=0,Executed=0;
 UINTN Literal=(UINTN)DegasApProbeMailbox-(UINTN)DegasApProbeStart;
 ARM_SMC_ARGS Smc={0};CHAR8 Text[80];
 EFI_CPU_ARCH_PROTOCOL *Cpu=NULL;
 BootWatchdogArmTenSeconds();
 if(Size<8 || Size>EFI_PAGE_SIZE || Literal>Size-8 || (Literal&7) || (ArmReadMpidr()&0xff00ffffffULL)!=0){Label(Gop,1690,"CPU PROBE GUARD FAILED");return;}
 for(I=0;I<2;I++){
  ZeroMem(&Smc,sizeof(Smc));Smc.Arg0=ARM_SMC_ID_PSCI_FEATURES;
  Smc.Arg1=I?ARM_SMC_ID_PSCI_CPU_OFF:ARM_SMC_ID_PSCI_CPU_ON_AARCH64;ArmCallSmc(&Smc);
  if((UINT32)Smc.Arg0!=0){Label(Gop,1690,"CPU PSCI FEATURE FAILED");return;}
 }
 for(I=1;I<8;I++)if(CpuAffinityState(I<<8)!=1){Label(Gop,1690,"CPU NOT OFF TEST STOPPED");return;}
 S=gBS->AllocatePages(AllocateMaxAddress,EfiBootServicesCode,1,&Code);
 if(EFI_ERROR(S)){Label(Gop,1690,"CPU CODE ALLOCATION FAILED");return;}
 S=gBS->AllocatePages(AllocateMaxAddress,EfiBootServicesData,1,&Mail);
 if(EFI_ERROR(S)){gBS->FreePages(Code,1);Label(Gop,1690,"CPU MAIL ALLOCATION FAILED");return;}
 S=gBS->LocateProtocol(&gEfiCpuArchProtocolGuid,NULL,(VOID **)&Cpu);
 if(EFI_ERROR(S)){Label(Gop,1690,"CPU MEMORY PROTOCOL FAILED");return;}
 CopyMem((VOID *)(UINTN)Code,DegasApProbeStart,Size);
 WriteBackInvalidateDataCacheRange((VOID *)(UINTN)Code,EFI_PAGE_SIZE);
 InvalidateInstructionCacheRange((VOID *)(UINTN)Code,EFI_PAGE_SIZE);
 ZeroMem((VOID *)(UINTN)Mail,EFI_PAGE_SIZE);
 WriteBackInvalidateDataCacheRange((VOID *)(UINTN)Mail,EFI_PAGE_SIZE);
 S=Cpu->SetMemoryAttributes(Cpu,Mail,EFI_PAGE_SIZE,EFI_MEMORY_UC|EFI_MEMORY_XP);
 DEBUG((DEBUG_ERROR,"DEGAS AP PROBE mailbox UC status=%r\n",S));
 if(EFI_ERROR(S)){Label(Gop,1690,"CPU MAIL UNCACHED FAILED");return;}
 DEBUG((DEBUG_ERROR,"DEGAS AP PROBE begin entry=%Lx mailbox=%Lx bytes=%u BSP_EL=%u\n",Code,Mail,(UINT32)Size,(UINT32)ArmReadCurrentEL()));
 for(Method=0;Method<2;Method++)for(I=1;I<8;I++){
  volatile UINT64 *Record=(volatile UINT64 *)(UINTN)(Mail+(Method*8+I)*256);UINT32 State;UINT64 Cookie=0x13570000ULL|I;
  if(CpuAffinityState(I<<8)!=1)goto Done;
  WriteUnaligned64((UINT64 *)(UINTN)(Code+Literal),(UINTN)Record);
  WriteBackInvalidateDataCacheRange((VOID *)(UINTN)Code,EFI_PAGE_SIZE);
  InvalidateInstructionCacheRange((VOID *)(UINTN)Code,EFI_PAGE_SIZE);
  MmioWrite32(0x1c00a008,0x1971);
  if(Method){
   Smc.Arg0=DegasCpuOnDirect(I<<8,Code,Cookie,(VOID *)(UINTN)(Record+16));
  }else{
   ZeroMem(&Smc,sizeof(Smc));Smc.Arg0=ARM_SMC_ID_PSCI_CPU_ON_AARCH64;Smc.Arg1=I<<8;Smc.Arg2=Code;Smc.Arg3=Cookie;
   Record[16]=Smc.Arg0;Record[17]=Smc.Arg1;Record[18]=Smc.Arg2;Record[19]=Smc.Arg3;
   ArmCallSmc(&Smc);
  }
  DEBUG((DEBUG_ERROR,"DEGAS AP PROBE method=%u cpu=%u CPU_ON=%08x wire=%Lx/%Lx/%Lx/%Lx expected=%Lx\n",(UINT32)Method,(UINT32)I,(UINT32)Smc.Arg0,Record[16],Record[17],Record[18],Record[19],Cookie));
  if((UINT32)Smc.Arg0!=0)goto Done;
  for(N=0;N<5000;N++){
   InvalidateDataCacheRange((VOID *)(UINTN)Record,256);MemoryFence();
   if(Record[0]==1 || Record[0]==2)break;MicroSecondDelay(100);
  }
  for(N=0;N<5000;N++){
   State=CpuAffinityState(I<<8);if(State==1)break;MicroSecondDelay(100);
  }
  InvalidateDataCacheRange((VOID *)(UINTN)Record,256);MemoryFence();
  DEBUG((DEBUG_ERROR,"DEGAS AP PROBE method=%u cpu=%u flag=%Lu mpidr=%Lx midr=%Lx EL=%Lu cntfrq=%Lu final_state=%08x raw_x0_x7=%Lx/%Lx/%Lx/%Lx/%Lx/%Lx/%Lx/%Lx\n",
   (UINT32)Method,(UINT32)I,Record[0],Record[1],Record[2],Record[3],Record[4],State,Record[6],Record[7],Record[8],Record[9],Record[10],Record[11],Record[12],Record[13]));
  if(State!=1)goto Done;
  if(Record[0]==1 && (Record[1]&0xff00ffffffULL)==(I<<8) && Record[4]==13000000){Executed++;if(Record[6]==Cookie)Passed++;}
 }
Done:
 DEBUG((DEBUG_ERROR,"DEGAS AP PROBE executed_and_off=%u/14 context_matches=%u/14 RESET REQUIRED\n",Executed,Passed));
 AsciiSPrint(Text,sizeof(Text),"AP EXECUTED %u OF 14",Executed);Label(Gop,1690,Text);
 AsciiSPrint(Text,sizeof(Text),"CONTEXT MATCH %u OF 14",Passed);Label(Gop,1770,Text);
 // Never free potentially executing code/mailboxes, even on timeout.
 // Mandatory reset prevents a failed AP from leaking into Windows startup.
 BootWatchdogArmTenSeconds();
}
