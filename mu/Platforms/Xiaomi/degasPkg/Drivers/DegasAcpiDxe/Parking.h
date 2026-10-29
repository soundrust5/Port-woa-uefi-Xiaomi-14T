// Experimental fallback for the observed missing PSCI CPU_ON context argument.
// Not automatic: invoked only by the explicit Windows boot menu action.
#include <Protocol/Cpu.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/TimerLib.h>
#include "../../Include/ApJournal.h"
#include "../../Include/WindowsObserverMap.h"
extern UINT8 DegasParkingStart[],DegasParkingEnd[];
extern UINT8 DegasWindowsObserverStart[],DegasWindowsObserverEnd[];
extern UINT8 DegasWindowsObserverRoot[],DegasWindowsObserverTcr[],DegasWindowsObserverMair[];
#if DEGAS_WINDOWS_BUGCHECK_MONITOR
#if DEGAS_BOOT_CORE != 0 || DEGAS_CPU_TEST_LIMIT != 3 || !DEGAS_WINDOWS_IDENTITY_OBSERVER
#error Bugcheck monitoring is an isolated CPU0/1/2 identity-checked experiment
#endif
extern UINT8 DegasBugcheckMonitorStart[],DegasApJournalCommit[];
#endif
STATIC UINT8 mParkingFadt[sizeof(mDegasFadt)];
STATIC BOOLEAN mParkingAttempted;
STATIC EFI_STATUS EFIAPI PrepareParking(VOID){
 EFI_PHYSICAL_ADDRESS Pages=0xffffffff,Observer=0xffffffff,Tables=0xffffffff;EFI_STATUS Status;EFI_CPU_ARCH_PROTOCOL *Cpu;
 UINTN Size=(UINTN)DegasParkingEnd-(UINTN)DegasParkingStart,I,N;
 ARM_SMC_ARGS Smc={0};
 UINT64 MailPages[8];DEGAS_WINDOWS_OBSERVER_MAP Map;
 UINTN ObserverBytes=(UINTN)DegasWindowsObserverEnd-(UINTN)DegasWindowsObserverStart;
 UINTN RootOff=(UINTN)DegasWindowsObserverRoot-(UINTN)DegasWindowsObserverStart;
 UINTN TcrOff=(UINTN)DegasWindowsObserverTcr-(UINTN)DegasWindowsObserverStart;
 UINTN MairOff=(UINTN)DegasWindowsObserverMair-(UINTN)DegasWindowsObserverStart;
 if(mParkingAttempted)return EFI_ALREADY_STARTED;
 mParkingAttempted=TRUE;
 if(DEGAS_BOOT_CORE!=0 || mEnabledCpuCount<2 || mEnabledCpuCount>8 || Size==0 || Size>0x600)return EFI_UNSUPPORTED;
 if(ObserverBytes<8 || ObserverBytes>4096 || RootOff>ObserverBytes-8 || TcrOff>ObserverBytes-8 || MairOff>ObserverBytes-8 || ((RootOff|TcrOff|MairOff)&7))return EFI_UNSUPPORTED;
 Status=gBS->LocateProtocol(&gEfiCpuArchProtocolGuid,NULL,(VOID **)&Cpu);if(EFI_ERROR(Status))return Status;
 // State must still match the original publication, before any AP is started.
 for(I=1;I<mEnabledCpuCount;I++){
  ZeroMem(&Smc,sizeof(Smc));Smc.Arg0=ARM_SMC_ID_PSCI_AFFINITY_INFO_AARCH64;Smc.Arg1=I<<8;ArmCallSmc(&Smc);
  if((UINT32)Smc.Arg0!=1)return EFI_NOT_READY;
 }
 Status=gBS->AllocatePages(AllocateMaxAddress,EfiACPIMemoryNVS,8,&Pages);if(EFI_ERROR(Status))return Status;
 ZeroMem((VOID *)(UINTN)Pages,8*EFI_PAGE_SIZE);
 Status=gBS->AllocatePages(AllocateMaxAddress,EfiACPIMemoryNVS,1,&Observer);if(EFI_ERROR(Status))return Status;
 Status=gBS->AllocatePages(AllocateMaxAddress,EfiACPIMemoryNVS,32,&Tables);if(EFI_ERROR(Status))return Status;
 ZeroMem((VOID *)(UINTN)Tables,32*EFI_PAGE_SIZE);
 for(I=0;I<8;I++)MailPages[I]=Pages+I*EFI_PAGE_SIZE;
 if(!DegasWindowsObserverBuild((VOID *)(UINTN)Tables,Tables,32,Observer,MailPages,&Map))return EFI_DEVICE_ERROR;
 CopyMem((VOID *)(UINTN)Observer,DegasWindowsObserverStart,ObserverBytes);
 WriteUnaligned64((UINT64 *)(UINTN)(Observer+RootOff),Map.Root);
 WriteUnaligned64((UINT64 *)(UINTN)(Observer+TcrOff),Map.Tcr);
 WriteUnaligned64((UINT64 *)(UINTN)(Observer+MairOff),Map.Mair);
 WriteBackDataCacheRange((VOID *)(UINTN)Tables,32*EFI_PAGE_SIZE);
 WriteBackInvalidateDataCacheRange((VOID *)(UINTN)Observer,EFI_PAGE_SIZE);
 InvalidateInstructionCacheRange((VOID *)(UINTN)Observer,EFI_PAGE_SIZE);
 Status=Cpu->SetMemoryAttributes(Cpu,Observer,EFI_PAGE_SIZE,EFI_MEMORY_WC);if(EFI_ERROR(Status))return Status;
 DEBUG((DEBUG_ERROR,"DEGAS COHERENT OBSERVER code=%Lx bytes=%u tables=%Lx count=%u\n",Observer,(UINT32)ObserverBytes,Tables,(UINT32)Map.TablesUsed));
 for(I=0;I<8;I++){
  UINT8 *Page=(UINT8 *)(UINTN)(Pages+I*EFI_PAGE_SIZE);
  WriteUnaligned32((UINT32 *)Page,MAX_UINT32);
  CopyMem(Page+0x800,DegasParkingStart,Size);
  WriteUnaligned64((UINT64 *)(Page+0xf40),Observer);
#if DEGAS_WINDOWS_BUGCHECK_MONITOR
  if(I==2){
   UINTN MonitorOff=(UINTN)DegasBugcheckMonitorStart-(UINTN)DegasWindowsObserverStart;
   UINTN CommitOff=(UINTN)DegasApJournalCommit-(UINTN)DegasParkingStart;
   if(MonitorOff>=ObserverBytes || CommitOff>=Size)return EFI_COMPROMISED_DATA;
   WriteUnaligned64((UINT64 *)(Page+0xf40),Observer+MonitorOff);
   WriteUnaligned64((UINT64 *)(Page+0xfd0),(UINTN)Page+0x800+CommitOff);
  }
#endif
 }
 WriteBackInvalidateDataCacheRange((VOID *)(UINTN)Pages,8*EFI_PAGE_SIZE);
 InvalidateInstructionCacheRange((VOID *)(UINTN)Pages,8*EFI_PAGE_SIZE);
 Status=Cpu->SetMemoryAttributes(Cpu,Pages,8*EFI_PAGE_SIZE,EFI_MEMORY_UC);
 DEBUG((DEBUG_ERROR,"DEGAS PARKING pages=%Lx UC=%r code_bytes=%u\n",Pages,Status,(UINT32)Size));
 if(EFI_ERROR(Status))return Status;
 // Snapshot any prior committed attempt, then initialize all AP-owned slots
 // before the first AP can execute or Windows can populate the mailboxes.
 if(!DegasApJournalInitialize(Pages))return EFI_DEVICE_ERROR;
 for(I=1;I<mEnabledCpuCount;I++){
  volatile UINT64 *Page=(volatile UINT64 *)(UINTN)(Pages+I*EFI_PAGE_SIZE);
  ZeroMem(&Smc,sizeof(Smc));Smc.Arg0=ARM_SMC_ID_PSCI_CPU_ON_AARCH64;Smc.Arg1=I<<8;
  Smc.Arg2=(UINTN)Page+0x800;Smc.Arg3=0;ArmCallSmc(&Smc);
  DEBUG((DEBUG_ERROR,"DEGAS PARKING cpu=%u CPU_ON=%08x\n",(UINT32)I,(UINT32)Smc.Arg0));
  if((UINT32)Smc.Arg0!=0)return EFI_DEVICE_ERROR;
  for(N=0;N<5000;N++){MemoryFence();if(Page[0xf00/8]==1)break;MicroSecondDelay(100);}
  DEBUG((DEBUG_ERROR,"DEGAS PARKING cpu=%u ready=%Lu mpidr=%Lx page=%p\n",(UINT32)I,Page[0xf00/8],Page[0xf08/8],Page));
  if(Page[0xf00/8]!=1 || (Page[0xf08/8]&0xff00ffffffULL)!=(I<<8))return EFI_TIMEOUT;
 }
 // Publish a single boot mechanism: parking, not mixed parking/PSCI flags.
 CopyMem(mParkingFadt,mDegasFadt,sizeof(mParkingFadt));
 WriteUnaligned16((UINT16 *)(mParkingFadt+129),0);
 mParkingFadt[9]=0;mParkingFadt[9]=CalculateCheckSum8(mParkingFadt,sizeof(mParkingFadt));
 for(I=0;I<8;I++){
  UINT8 *Entry=mRuntimeMadt+44+80*I;
  WriteUnaligned32((UINT32 *)(Entry+16),I<mEnabledCpuCount?1:0);
  WriteUnaligned64((UINT64 *)(Entry+24),I<mEnabledCpuCount?Pages+I*EFI_PAGE_SIZE:0);
 }
 mRuntimeMadt[9]=0;mRuntimeMadt[9]=CalculateCheckSum8(mRuntimeMadt,sizeof(mRuntimeMadt));
 Status=mAcpiProtocol->UninstallAcpiTable(mAcpiProtocol,mAcpiKeys[2]);if(EFI_ERROR(Status))return Status;
 Status=mAcpiProtocol->UninstallAcpiTable(mAcpiProtocol,mAcpiKeys[1]);if(EFI_ERROR(Status))return Status;
 Status=mAcpiProtocol->InstallAcpiTable(mAcpiProtocol,mParkingFadt,sizeof(mParkingFadt),&mAcpiKeys[1]);if(EFI_ERROR(Status))return Status;
 Status=mAcpiProtocol->InstallAcpiTable(mAcpiProtocol,mRuntimeMadt,sizeof(mRuntimeMadt),&mAcpiKeys[2]);if(EFI_ERROR(Status))return Status;
 DEBUG((DEBUG_ERROR,"DEGAS PARKING READY aps=%u identity_observer=%u FADT_PSCI=0 GICC_PPV=1 NVS_RESERVED RESET_IF_BOOT_FAILS\n",mEnabledCpuCount-1,(UINT32)DEGAS_WINDOWS_IDENTITY_OBSERVER));
#if DEGAS_WINDOWS_BUGCHECK_MONITOR
 DEBUG((DEBUG_ERROR,"DEGAS BUGCHECK MONITOR CPU2 advertised and still parked; aborts on OS release\n"));
#endif
 return VerifyPublished();
}
