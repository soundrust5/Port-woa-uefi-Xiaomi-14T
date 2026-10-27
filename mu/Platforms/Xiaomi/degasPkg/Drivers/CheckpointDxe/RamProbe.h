// HISTORICAL RESERVED-REGION TEST ONLY. Not included in allocator firmware.
// Never call this once the range is ConventionalMemory: firmware may own pages.
#include <Library/CacheMaintenanceLib.h>
#include "RamProbeCore.h"
STATIC VOID RamReadPage(VOID *Context,UINT64 Address,UINT64 *Buffer){
 (VOID)Context;
 WriteBackInvalidateDataCacheRange((VOID *)(UINTN)Address,DEGAS_RAM_PROBE_PAGE);
 ArmDataSynchronizationBarrier();
 CopyMem(Buffer,(VOID *)(UINTN)Address,DEGAS_RAM_PROBE_PAGE);
}
STATIC VOID RamWritePage(VOID *Context,UINT64 Address,CONST UINT64 *Buffer){
 (VOID)Context;
 CopyMem((VOID *)(UINTN)Address,Buffer,DEGAS_RAM_PROBE_PAGE);
 WriteBackInvalidateDataCacheRange((VOID *)(UINTN)Address,DEGAS_RAM_PROBE_PAGE);
 ArmDataSynchronizationBarrier();
}
STATIC VOID RamProgress(VOID *Context,UINT32 Phase,UINT32 Page){
 (VOID)Context;
 MmioWrite32(0x1c00a008,0x1971);
 if(Page%64==0 || Page==DEGAS_RAM_PROBE_COUNT-1)
  DEBUG((DEBUG_ERROR,"DEGAS RAM phase=%u page=%u address=%016lx\n",Phase,Page,RamPageAddress(Page)));
}
STATIC VOID RamAccessTest(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop){
 UINT64 *Backup,*Work,*Read;
 DEGAS_RAM_RESULT R;
 CONST DEGAS_RAM_IO Io={NULL,RamReadPage,RamWritePage,RamProgress};
 UINTN Bytes=(UINTN)DEGAS_RAM_PROBE_COUNT*DEGAS_RAM_PROBE_PAGE;
 Backup=AllocatePool(Bytes+2*DEGAS_RAM_PROBE_PAGE);
 if(Backup==NULL){Label(Gop,1690,"RAM BACKUP ALLOCATION FAILED");return;}
 Work=Backup+Bytes/sizeof(UINT64);Read=Work+DEGAS_RAM_PROBE_PAGE/sizeof(UINT64);
 Label(Gop,1690,"CHECKING RAM SAMPLES");
 DEBUG((DEBUG_ERROR,"DEGAS RAM begin base=%016lx size=%016lx pages=%u bytes=%Lu RESERVED NOT ALLOCATABLE\n",
  DEGAS_RAM_PROBE_BASE,DEGAS_RAM_PROBE_SIZE,(UINT32)DEGAS_RAM_PROBE_COUNT,(UINT64)Bytes));
 RamProbeRun(&Io,Backup,Work,Read,&R);
 DEBUG((DEBUG_ERROR,"DEGAS RAM result=%u restored=%u failed_phase=%u address=%016lx expected=%016lx actual=%016lx\n",
  R.Error,R.Restored,R.FailedPhase,R.FailedAddress,R.Expected,R.Actual));
 FreePool(Backup);
 Label(Gop,1690,!R.Restored?"RAM RESTORE FAILED":R.Error?"RAM SAMPLE FAILED":"RAM SAMPLES PASS RESTORED");
}
