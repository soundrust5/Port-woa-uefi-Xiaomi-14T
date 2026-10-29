// Native PSCI remains enabled. No CPU_ON is issued from this preparation.
#include "../../Include/Protocol/DegasNativeContext.h"
STATIC DEGAS_NATIVE_CONTEXT_PROTOCOL mNativeContext;
STATIC EFI_STATUS PrepareNativeContext(VOID){
 EFI_PHYSICAL_ADDRESS Pages=0xffffffffULL;EFI_STATUS S;UINTN I;
 ARM_SMC_ARGS Smc;
 if(DEGAS_BOOT_CORE!=0 || mEnabledCpuCount!=DEGAS_CPU_TEST_LIMIT ||
    (DEGAS_CPU_TEST_LIMIT!=2 && DEGAS_CPU_TEST_LIMIT!=8) || DEGAS_ACPI_CPU_COUNT!=8 ||
    mRuntimeMadt[8]!=5 || ReadUnaligned16((CONST UINT16 *)(mDegasFadt+129))!=1)return EFI_UNSUPPORTED;
 // The prefix uses only Aff1 after this complete topology qualification.
 for(I=0;I<8;I++){
  CONST UINT8 *E=mRuntimeMadt+44+80*I;
  if(E[0]!=11 || E[1]!=80 || ReadUnaligned32((CONST UINT32 *)(E+4)) ||
     ReadUnaligned32((CONST UINT32 *)(E+8))!=I || ReadUnaligned32((CONST UINT32 *)(E+12))!=(I<DEGAS_CPU_TEST_LIMIT?1U:0U) ||
     ReadUnaligned64((CONST UINT64 *)(E+68))!=((UINT64)I<<8))return EFI_COMPROMISED_DATA;
  ZeroMem(&Smc,sizeof(Smc));Smc.Arg0=ARM_SMC_ID_PSCI_AFFINITY_INFO_AARCH64;Smc.Arg1=I<<8;ArmCallSmc(&Smc);
  if((UINT32)Smc.Arg0!=(I?1U:0U))return EFI_NOT_READY;
 }
 S=gBS->AllocatePages(AllocateMaxAddress,EfiACPIMemoryNVS,8,&Pages);if(EFI_ERROR(S))return S;
 if(!Pages || (Pages&4095) || Pages>0xffff8000ULL){gBS->FreePages(Pages,8);return EFI_COMPROMISED_DATA;}
 ZeroMem((VOID *)(UINTN)Pages,8*4096);
 WriteBackDataCacheRange((VOID *)(UINTN)Pages,8*4096);MemoryFence();
 for(I=0;I<8;I++){
  UINT8 *E=mRuntimeMadt+44+80*I;
  WriteUnaligned32((UINT32 *)(E+16),0); // Not a parking protocol advertisement.
  WriteUnaligned64((UINT64 *)(E+24),I<DEGAS_CPU_TEST_LIMIT?Pages+I*4096:0);
 }
 mRuntimeMadt[9]=0;mRuntimeMadt[9]=CalculateCheckSum8(mRuntimeMadt,sizeof(mRuntimeMadt));
 mNativeContext.Version=1;mNativeContext.Mode=DEGAS_NATIVE_CONTEXT_BRIDGE;
 mNativeContext.MailboxBase=Pages;mNativeContext.CpuCount=DEGAS_CPU_TEST_LIMIT;mNativeContext.BootCore=0;
 DEBUG((DEBUG_ERROR,"DEGAS NATIVE CONTEXT prepared mode=%u pages=%Lx count=%u PSCI=1 APs_OFF=1\n",mNativeContext.Mode,Pages,mNativeContext.CpuCount));
 return EFI_SUCCESS;
}
