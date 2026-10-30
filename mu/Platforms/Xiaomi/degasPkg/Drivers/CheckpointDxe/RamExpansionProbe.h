// Only generated RESERVED candidates. Never probe allocator-owned memory.
#include <Library/CacheMaintenanceLib.h>
#include "../../Include/RamExpansionRanges.h"
STATIC VOID RamExpansionProbe(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop){
 UINT64 *Backup=AllocatePool(12288),*Pattern,*Read;UINTN Range,I,Pass;
 UINT64 Address,Next,Pages=0,Expected,Actual;BOOLEAN Ok=TRUE,Restored=TRUE;
 if(!Backup){Label(Gop,1900,"RAM TEST ALLOCATION FAILED");return;}
 Pattern=Backup+512;Read=Pattern+512;
 BootWatchdogArmTenSeconds();Label(Gop,1900,"CHECKING EXTRA RAM");
 DEBUG((DEBUG_ERROR,"DEGAS RAMEXP begin ranges=%u RESERVED ONLY\n",(UINT32)ARRAY_SIZE(mRamExpansionRanges)));
 for(Range=0;Range<ARRAY_SIZE(mRamExpansionRanges) && Ok;Range++){
  UINT64 Base=mRamExpansionRanges[Range].Base,End=mRamExpansionRanges[Range].End;
  for(Address=Base;Address<End;Address=Next){
   Next=Address+0x200000; if(Next>=End)Next=Address==End-4096?End:End-4096;
   MmioWrite32(0x1c00a008,0x1971);
   WriteBackInvalidateDataCacheRange((VOID *)(UINTN)Address,4096);
   CopyMem(Backup,(VOID *)(UINTN)Address,4096);
   for(Pass=0;Pass<2;Pass++){
    for(I=0;I<512;I++)Pattern[I]=(Address+I*8)^((Pass?0x5555555555555555ULL:0xaaaaaaaaaaaaaaaaULL));
    CopyMem((VOID *)(UINTN)Address,Pattern,4096);
    WriteBackInvalidateDataCacheRange((VOID *)(UINTN)Address,4096);ArmDataSynchronizationBarrier();
    CopyMem(Read,(VOID *)(UINTN)Address,4096);
    for(I=0;I<512;I++)if(Read[I]!=Pattern[I]){
     Expected=Pattern[I];Actual=Read[I];Ok=FALSE;
     DEBUG((DEBUG_ERROR,"DEGAS RAMEXP mismatch pa=%016lx expected=%016lx actual=%016lx\n",Address+I*8,Expected,Actual));break;
    }
    if(!Ok)break;
   }
   CopyMem((VOID *)(UINTN)Address,Backup,4096);
   WriteBackInvalidateDataCacheRange((VOID *)(UINTN)Address,4096);ArmDataSynchronizationBarrier();
   CopyMem(Read,(VOID *)(UINTN)Address,4096);
   if(CompareMem(Read,Backup,4096)){Restored=FALSE;Ok=FALSE;}
   Pages++;if(!Ok)break;
  }
  DEBUG((DEBUG_ERROR,"DEGAS RAMEXP range=%u base=%016lx end=%016lx pages=%Lu ok=%u restored=%u\n",(UINT32)Range,Base,End,Pages,Ok,Restored));
 }
 DEBUG((DEBUG_ERROR,"DEGAS RAMEXP result=%u restored=%u pages=%Lu\n",Ok,Restored,Pages));
 FreePool(Backup);
 Label(Gop,1900,!Restored?"RAM RESTORE FAILED RESET":Ok?"EXTRA RAM SAMPLES PASS RESTORED":"EXTRA RAM SAMPLE FAILED");
 if(!Restored)CpuDeadLoop();
}
