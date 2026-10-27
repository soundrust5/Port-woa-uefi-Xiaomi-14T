#include <Guid/SmBios.h>
#include "FrameSample.h"

STATIC BOOLEAN mBootObservationActive;
STATIC UINT64 mBootObservationTick;
STATIC UINT32 mBootObservationCount;

// Only the already mapped, fixed platform framebuffer is sampled. No GOP,
// allocation, filesystem, cache maintenance, or display-register writes here.
STATIC VOID BootObservation(CONST CHAR8 *Phase,BOOLEAN Force){
 DEGAS_FRAME_SAMPLE F;UINT64 Now=GetPerformanceCounter();
 if(!mBootObservationActive)return;
 if(!Force && GetTimeInNanoSecond(Now-mBootObservationTick)<2000000000ULL)return;
 mBootObservationTick=Now;
 if(!DegasFrameSample((volatile UINT32 *)(UINTN)0xfcc2f000,0xcea000,1220,2712,1248,&F))return;
 DEBUG((DEBUG_ERROR,"DEGAS BOOT OBS phase=%a sample=%u us=%Lu reads=%u wdt=%08x length=%08x rgb=%08x alpha=%08x sampled=%u zero_alpha=%u opaque=%u nonblack=%u\n",
  Phase,++mBootObservationCount,GetTimeInNanoSecond(Now-mBootBudgetStart)/1000,
  mUfsReads,MmioRead32(0x1c00a000),MmioRead32(0x1c00a004),
  F.RgbHash,F.AlphaHash,F.Pixels,F.ZeroAlpha,F.OpaqueAlpha,F.Nonblack));
}

// Called before StartImage at application TPL, never from EBS or the timer.
// These are observations of missing capabilities, not boot-blocking checks.
STATIC VOID BootPreflight(VOID){
 EFI_STATUS S;EFI_TIME Time;EFI_MEMORY_DESCRIPTOR *Map=NULL,*D;
 UINTN I,Size=0,Capacity,Key,DescSize=0,Attempt;UINT32 Version;
 UINT64 Free=0,Largest=0,Low=0,Bytes;BOOLEAN Smbios=FALSE,Smbios3=FALSE;
 for(I=0;I<gST->NumberOfTableEntries;I++){
  if(CompareGuid(&gST->ConfigurationTable[I].VendorGuid,&gEfiSmbiosTableGuid))Smbios=TRUE;
  if(CompareGuid(&gST->ConfigurationTable[I].VendorGuid,&gEfiSmbios3TableGuid))Smbios3=TRUE;
 }
 ZeroMem(&Time,sizeof(Time));S=gST->RuntimeServices->GetTime(&Time,NULL);
 DEBUG((DEBUG_ERROR,"DEGAS BOOT PREFLIGHT smbios2=%u smbios3=%u gettime=%r date=%u-%u-%u time=%u:%u:%u\n",
  Smbios,Smbios3,S,Time.Year,Time.Month,Time.Day,Time.Hour,Time.Minute,Time.Second));
 S=gBS->GetMemoryMap(&Size,NULL,&Key,&DescSize,&Version);
 if(S!=EFI_BUFFER_TOO_SMALL || DescSize<sizeof(*Map))goto Done;
 for(Attempt=0;Attempt<3;Attempt++){
  if(Size>0x100000 || DescSize>0x1000){S=EFI_BAD_BUFFER_SIZE;goto Done;}
  Capacity=Size+16*DescSize;Map=AllocatePool(Capacity);
  if(!Map){S=EFI_OUT_OF_RESOURCES;goto Done;}
  Size=Capacity;S=gBS->GetMemoryMap(&Size,Map,&Key,&DescSize,&Version);
  if(S!=EFI_BUFFER_TOO_SMALL)break;
  FreePool(Map);Map=NULL;
 }
 if(EFI_ERROR(S))goto Done;
 if(DescSize<sizeof(*Map) || Size%DescSize){S=EFI_COMPROMISED_DATA;goto Done;}
 for(I=0;I<Size;I+=DescSize){
  D=(VOID *)((UINT8 *)Map+I);
  if(D->Type!=EfiConventionalMemory)continue;
  if(D->NumberOfPages>MAX_UINT64/4096){S=EFI_COMPROMISED_DATA;goto Done;}
  Bytes=D->NumberOfPages*4096;
  if(Bytes>MAX_UINT64-Free || Bytes>MAX_UINT64-D->PhysicalStart){S=EFI_COMPROMISED_DATA;goto Done;}
  Free+=Bytes;if(Bytes>Largest)Largest=Bytes;
  if(D->PhysicalStart<0x100000000ULL)Low+=MIN(Bytes,0x100000000ULL-D->PhysicalStart);
 }
Done:
 DEBUG((DEBUG_ERROR,"DEGAS BOOT PREFLIGHT map=%r conventional=%Lu largest_free=%Lu free_below_4g=%Lu\n",S,Free,Largest,Low));
 if(Map)FreePool(Map);
}
