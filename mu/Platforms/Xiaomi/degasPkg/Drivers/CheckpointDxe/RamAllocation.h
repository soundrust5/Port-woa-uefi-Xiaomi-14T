// Operate only on pages owned through AllocatePages, never raw free RAM.
#include <Library/CacheMaintenanceLib.h>
#include "RamAllocationCore.h"
STATIC EFI_STATUS RamLogMemoryMap(VOID){
 EFI_MEMORY_DESCRIPTOR *Map=NULL,*D;
 EFI_STATUS S;UINTN Size=0,Key,DescSize=0,Offset,Attempt;UINT32 Version;
 UINT64 Conventional=0,Window=0,Start,End,A,B;
 S=gBS->GetMemoryMap(&Size,NULL,&Key,&DescSize,&Version);
 if(S!=EFI_BUFFER_TOO_SMALL || DescSize<sizeof(*Map))return EFI_DEVICE_ERROR;
 for(Attempt=0;Attempt<3;Attempt++){
  Size+=8*DescSize;Map=AllocatePool(Size);
  if(Map==NULL)return EFI_OUT_OF_RESOURCES;
  S=gBS->GetMemoryMap(&Size,Map,&Key,&DescSize,&Version);
  if(S!=EFI_BUFFER_TOO_SMALL)break;
  FreePool(Map);Map=NULL;
 }
 if(EFI_ERROR(S)){if(Map)FreePool(Map);return S;}
 if(DescSize<sizeof(*Map) || Size%DescSize){FreePool(Map);return EFI_COMPROMISED_DATA;}
 for(Offset=0;Offset<Size;Offset+=DescSize){
  D=(VOID *)((UINT8 *)Map+Offset);
  if(D->Type!=EfiConventionalMemory)continue;
  Start=D->PhysicalStart;End=Start+EFI_PAGES_TO_SIZE(D->NumberOfPages);
  Conventional+=End-Start;
  A=MAX(Start,DEGAS_RAM_PROBE_BASE);B=MIN(End,DEGAS_RAM_PROBE_BASE+DEGAS_RAM_PROBE_SIZE);
  if(A<B)Window+=B-A;
 }
 DEBUG((DEBUG_ERROR,"DEGAS RAM ALLOC map conventional=%Lu window_free=%Lu descriptor=%u\n",
  Conventional,Window,(UINT32)DescSize));
 FreePool(Map);
 return Window>=DEGAS_RAM_ALLOC_BYTES ? EFI_SUCCESS : EFI_OUT_OF_RESOURCES;
}
STATIC VOID RamAccessTest(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop){
 EFI_PHYSICAL_ADDRESS Address=DEGAS_RAM_PROBE_BASE+DEGAS_RAM_PROBE_SIZE-1;
 EFI_STATUS Status,FreeStatus;UINT64 Offset,Word,Actual,Expected;UINT32 Pass;
 UINTN Pages=EFI_SIZE_TO_PAGES(DEGAS_RAM_ALLOC_BYTES);volatile UINT64 *P;
 Status=RamLogMemoryMap();
 if(EFI_ERROR(Status)){Label(Gop,1690,"RAM MAP CHECK FAILED");return;}
 Status=gBS->AllocatePages(AllocateMaxAddress,EfiBootServicesData,Pages,&Address);
 DEBUG((DEBUG_ERROR,"DEGAS RAM ALLOC allocation=%r base=%016lx bytes=%Lu\n",Status,Address,DEGAS_RAM_ALLOC_BYTES));
 if(EFI_ERROR(Status)){Label(Gop,1690,"RAM ALLOCATION FAILED");return;}
 if(!RamAllocationInWindow(Address,DEGAS_RAM_ALLOC_BYTES)){
  Status=EFI_UNSUPPORTED;goto Release;
 }
 Label(Gop,1690,"CHECKING ALLOCATED RAM");
 for(Pass=1;Pass<=2;Pass++){
  for(Offset=0;Offset<DEGAS_RAM_ALLOC_BYTES;Offset+=DEGAS_RAM_ALLOC_CHUNK){
   MmioWrite32(0x1c00a008,0x1971);P=(VOID *)(UINTN)(Address+Offset);
   for(Word=0;Word<DEGAS_RAM_ALLOC_CHUNK/8;Word++)P[Word]=RamAllocationPattern(Address+Offset+8*Word,Pass);
   WriteBackInvalidateDataCacheRange((VOID *)(UINTN)(Address+Offset),DEGAS_RAM_ALLOC_CHUNK);
   ArmDataSynchronizationBarrier();
  }
  DEBUG((DEBUG_ERROR,"DEGAS RAM ALLOC pass=%u written=%Lu\n",Pass,DEGAS_RAM_ALLOC_BYTES));
  for(Offset=0;Offset<DEGAS_RAM_ALLOC_BYTES;Offset+=DEGAS_RAM_ALLOC_CHUNK){
   MmioWrite32(0x1c00a008,0x1971);P=(VOID *)(UINTN)(Address+Offset);
   WriteBackInvalidateDataCacheRange((VOID *)(UINTN)(Address+Offset),DEGAS_RAM_ALLOC_CHUNK);
   ArmDataSynchronizationBarrier();
   for(Word=0;Word<DEGAS_RAM_ALLOC_CHUNK/8;Word++){
    Actual=P[Word];Expected=RamAllocationPattern(Address+Offset+8*Word,Pass);
    if(Actual!=Expected){
     DEBUG((DEBUG_ERROR,"DEGAS RAM ALLOC mismatch pass=%u address=%016lx expected=%016lx actual=%016lx\n",
      Pass,Address+Offset+8*Word,Expected,Actual));Status=EFI_DEVICE_ERROR;goto Release;
    }
   }
  }
  DEBUG((DEBUG_ERROR,"DEGAS RAM ALLOC pass=%u verified=%Lu\n",Pass,DEGAS_RAM_ALLOC_BYTES));
 }
Release:
 FreeStatus=gBS->FreePages(Address,Pages);
 DEBUG((DEBUG_ERROR,"DEGAS RAM ALLOC result=%r free=%r\n",Status,FreeStatus));
 Label(Gop,1690,EFI_ERROR(FreeStatus)?"RAM FREE FAILED":EFI_ERROR(Status)?"RAM ALLOCATION TEST FAILED":"RAM ALLOCATION PASS");
}
