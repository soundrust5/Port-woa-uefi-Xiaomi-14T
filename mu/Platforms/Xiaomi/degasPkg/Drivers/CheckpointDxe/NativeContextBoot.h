#ifndef DEGAS_NATIVE_CONTEXT_BRIDGE
#define DEGAS_NATIVE_CONTEXT_BRIDGE 0
#endif
#if DEGAS_NATIVE_CONTEXT_BRIDGE
#if DEGAS_NATIVE_CONTEXT_BRIDGE > 2 || DEGAS_CPU_PARKING_EXPERIMENT || DEGAS_BOOT_CORE != 0 || (DEGAS_CPU_TEST_LIMIT != 2 && DEGAS_CPU_TEST_LIMIT != 8)
#error Native bridge requires CPU0 native PSCI experiment and two or eight CPUs
#endif
#include "../../Include/Protocol/DegasNativeContext.h"
#include "../../Include/NativeContextCore.h"
#include "../../Include/NativeContextRecord.h"
STATIC DEGAS_NATIVE_CONTEXT_PROTOCOL *mNativeBoot;
STATIC UINT8 *mNativeRawMap;
STATIC DEGAS_CONTEXT_REGION *mNativeRegions;
STATIC DEGAS_CONTEXT_DOMAIN mNativeDomain;
STATIC BOOLEAN mNativeMapReady;
STATIC EFI_STATUS mNativeMapStatus;
extern UINT64 DegasNativeTranslate(UINT64 Address,UINT32 Write);
STATIC BOOLEAN NativeAccess(UINT64 Address,UINTN Bytes,BOOLEAN Write){
 UINT64 Page,End,Par;
 if(!Bytes || Address>MAX_UINT64-Bytes)return FALSE;End=Address+Bytes;
 for(Page=Address&~4095ULL;Page<End;Page+=4096){
  Par=DegasNativeTranslate(Page,Write?1:0);
  if((Par&1) || (Par&0x000ffffffffff000ULL)!=Page){
   mNativeAccessPage=Page;mNativeAccessPar=Par;
   DEBUG((DEBUG_ERROR,"DEGAS NATIVE ACCESS rejected page=%Lx write=%u par=%Lx\n",Page,Write,Par));return FALSE;
  }
 }
 return TRUE;
}
STATIC BOOLEAN NativeRead(VOID *Context,UINT64 Address,VOID *Data,UINTN Bytes){
 (VOID)Context;
 if(!DegasContextCovered(&mNativeDomain,Address,Bytes) || !NativeAccess(Address,Bytes,FALSE))return FALSE;
 CopyMem(Data,(CONST VOID *)(UINTN)Address,Bytes);return TRUE;
}
STATIC BOOLEAN NativeWrite(VOID *Context,UINT64 Address,CONST VOID *Data,UINTN Bytes){
 (VOID)Context;
 if(!DegasContextWritable(&mNativeDomain,Address,Bytes) || !NativeAccess(Address,Bytes,TRUE))return FALSE;
 CopyMem((VOID *)(UINTN)Address,Data,Bytes);return TRUE;
}
STATIC EFI_STATUS NativeBootPrepare(VOID){
 EFI_GUID Guid=DEGAS_NATIVE_CONTEXT_GUID;EFI_STATUS S;
 S=gBS->LocateProtocol(&Guid,NULL,(VOID **)&mNativeBoot);if(EFI_ERROR(S))return S;
 if(!mNativeBoot || mNativeBoot->Version!=1 || mNativeBoot->Mode!=DEGAS_NATIVE_CONTEXT_BRIDGE ||
    mNativeBoot->CpuCount!=DEGAS_CPU_TEST_LIMIT || mNativeBoot->BootCore || !DegasContextMailboxValid(mNativeBoot->MailboxBase))return EFI_UNSUPPORTED;
 // Allocate before starting the loader; the EBS callback never allocates and
 // never changes memory-map ownership or page protection.
 mNativeRawMap=AllocatePool(0x10000);
 mNativeRegions=AllocatePool(1024*sizeof(*mNativeRegions));
 if(!mNativeRawMap || !mNativeRegions)return EFI_OUT_OF_RESOURCES;
 ZeroMem(&mNativeDomain,sizeof(mNativeDomain));
 mNativeDomain.Read=NativeRead;mNativeDomain.Write=NativeWrite;
 mNativeDomain.Regions=mNativeRegions;
 DegasNativeRecord(1,mNativeBoot,NULL,0,0);
 DEBUG((DEBUG_ERROR,"DEGAS NATIVE CONTEXT armed mode=%u pages=%Lx source56byte RAM only\n",mNativeBoot->Mode,mNativeBoot->MailboxBase));
 return EFI_SUCCESS;
}
// Snapshot while memory services are explicitly still live. EBS processing
// later consumes this immutable domain without calling any Boot Service.
STATIC VOID NativeBootSnapshot(VOID){
 EFI_STATUS S;UINTN Size=0x10000,Key,Stride=0,I,Count;UINT32 Version=0;
 mNativeMapReady=FALSE;mNativeMapStatus=EFI_NOT_READY;
 if(!mNativeBoot || !mNativeRawMap || !mNativeRegions)return;
 S=gBS->GetMemoryMap(&Size,(VOID *)mNativeRawMap,&Key,&Stride,&Version);
 mNativeMapStatus=S;
 if(EFI_ERROR(S) || Stride<sizeof(EFI_MEMORY_DESCRIPTOR) || Stride>0x1000 || Size%Stride || Size/Stride>1024){
  return;
 }
 Count=Size/Stride;
 for(I=0;I<Count;I++){
  CONST EFI_MEMORY_DESCRIPTOR *D=(CONST VOID *)(mNativeRawMap+I*Stride);
  mNativeRegions[I].Base=D->PhysicalStart;mNativeRegions[I].Pages=D->NumberOfPages;
  mNativeRegions[I].Type=D->Type;mNativeRegions[I].Attributes=D->Attribute;
 }
 // Sort only our private snapshot; overlaps remain a fatal core-validation error.
 for(I=1;I<Count;I++){
  DEGAS_CONTEXT_REGION R=mNativeRegions[I];UINTN J=I;
  while(J && mNativeRegions[J-1].Base>R.Base){mNativeRegions[J]=mNativeRegions[J-1];J--;}
  mNativeRegions[J]=R;
 }
 mNativeDomain.RegionCount=Count;mNativeMapReady=TRUE;
}
STATIC VOID NativeBootExit(VOID){
 DEGAS_CONTEXT_RESULT R;UINTN Count=mNativeDomain.RegionCount;
 ZeroMem(&R,sizeof(R));R.Status=DegasContextBadMap;
 if(!mNativeMapReady){
  if(mNativeBoot)DegasNativeRecord(2,mNativeBoot,&R,0,mNativeMapStatus);
  CpuDeadLoop();
 }
 DegasContextLocate(&mNativeDomain,&R);
 DegasNativeRecord(2,mNativeBoot,&R,(UINT32)Count,mNativeMapStatus);
 DEBUG((DEBUG_ERROR,"DEGAS NATIVE CONTEXT locate status=%u pages=%u mz=%u matches=%u phys=%Lx virt=%Lx\n",
  R.Status,R.Pages,R.MzCandidates,R.Matches,R.KernelBase,R.KernelVirtualBase));
 if(R.Status)CpuDeadLoop(); // Recovery is manual when diagnostic watchdog is off.
 if(mNativeBoot->Mode==2){
  DegasContextApply(&mNativeDomain,mNativeBoot->MailboxBase,&R);
  if(R.Patched){
   WriteBackDataCacheRange((VOID *)(UINTN)(R.KernelBase+DEGAS_BRIDGE_PREFIX_RVA),DEGAS_BRIDGE_PREFIX_SIZE);
   InvalidateInstructionCacheRange((VOID *)(UINTN)(R.KernelBase+DEGAS_BRIDGE_PREFIX_RVA),DEGAS_BRIDGE_PREFIX_SIZE);
   MemoryFence();
  }
 }
 DegasNativeRecord(3,mNativeBoot,&R,(UINT32)Count,mNativeMapStatus);
 DEBUG((DEBUG_ERROR,"DEGAS NATIVE CONTEXT complete status=%u attempted=%u patched=%u\n",R.Status,R.WriteAttempted,R.Patched));
 if(R.Status || (mNativeBoot->Mode==2 && !R.Patched))CpuDeadLoop();
}
#endif
