// Menu-only controller observation. It never enables DVFS or writes a request.
#include "UefiCsramJournal.h"
extern UINT64 DegasNativeTranslate(UINT64 Address,UINT32 Write);
extern UINT32 DegasDvfsPmuRead(UINT64 *Words);
STATIC UINT32 mDvfsSnapshotSequence;
STATIC UINT32 mDvfsJournal[512];
STATIC UINTN mDvfsJournalPosition;
STATIC BOOLEAN mDvfsJournalComplete;
STATIC BOOLEAN EFIAPI DvfsQualify(VOID *Context,UINT64 Address){
 UINT64 Par;(VOID)Context;
 if(!DegasDvfsAddressAllowed(Address))return FALSE;
 Par=DegasNativeTranslate(Address,0);
 // Actual current EL1 identity-read permission, not an EFI map assertion.
 // This does not prove EL2 permission; the watchdog covers an access abort.
 return !(Par&1) && (Par&0x000ffffffffff000ULL)==(Address&~4095ULL);
}
STATIC BOOLEAN EFIAPI DvfsRead(VOID *Context,UINT64 Address,UINT32 *Value){
 (VOID)Context;
 if(!Value || !DegasDvfsAddressAllowed(Address))return FALSE;
 *Value=MmioRead32((UINTN)Address);return TRUE;
}
STATIC VOID EFIAPI DvfsEmit(VOID *Context,CONST CHAR8 *Line){
 UINTN Bytes=AsciiStrLen(Line);(VOID)Context;
 DEBUG((DEBUG_ERROR,"%a",Line));
 if(Bytes>sizeof(mDvfsJournal)-12-mDvfsJournalPosition){mDvfsJournalComplete=FALSE;return;}
 CopyMem((UINT8 *)mDvfsJournal+12+mDvfsJournalPosition,Line,Bytes);
 mDvfsJournalPosition+=Bytes;
}
STATIC BOOLEAN DvfsPublish(VOID){
 volatile UINT32 *Zone=(volatile UINT32 *)(UINTN)DEGAS_APJ_BASE;UINTN I;
 if(!mDvfsJournalComplete || mDvfsJournalPosition>sizeof(mDvfsJournal)-12)return FALSE;
 mDvfsJournal[0]=0x43474244U;
 mDvfsJournal[1]=mDvfsJournal[2]=(UINT32)mDvfsJournalPosition;
 // First 2KiB only. The Android route flag at +F00 remains untouched.
 Zone[0]=0;MemoryFence();
 for(I=3;I<ARRAY_SIZE(mDvfsJournal);I++)Zone[I]=mDvfsJournal[I];
 Zone[1]=mDvfsJournal[1];Zone[2]=mDvfsJournal[2];MemoryFence();
 Zone[0]=mDvfsJournal[0];MemoryFence();
 for(I=0;I<ARRAY_SIZE(mDvfsJournal);I++)if(Zone[I]!=mDvfsJournal[I]){
  Zone[0]=0;MemoryFence();return FALSE;
 }
 return TRUE;
}
#include "CpuDvfsStep.h"
STATIC VOID CpuDvfsSnapshot(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop){
 STATIC UINT32 Previous[DEGAS_APJ_WORDS];DEGAS_DVFS_SNAPSHOT S;
 UINT32 Status,D;BOOLEAN Saved;CHAR8 Text[64];
 BootWatchdogArmTenSeconds();
 DegasApJournalRead(Previous);
 if(!DegasApJournalReportAndDump(Previous)){
  Label(Gop,1690,"JOURNAL BACKUP FAILED");return;
 }
 ZeroMem(mDvfsJournal,sizeof(mDvfsJournal));
 CopyMem((UINT8 *)mDvfsJournal+12,"====0.000000-D\n",15);
 mDvfsJournalPosition=15;mDvfsJournalComplete=TRUE;
 DEBUG((DEBUG_ERROR,"DEGAS DVFS snapshot read-only boot-stage observer begin\n"));
 Status=DegasDvfsCaptureUefi(DvfsQualify,DvfsRead,NULL,DegasDvfsStageMenu,
  ++mDvfsSnapshotSequence,&S);
 DegasDvfsJournalUefi(&S,DvfsEmit,NULL);Saved=DvfsPublish();
 DEBUG((DEBUG_ERROR,"DEGAS DVFS retained saved=%u status=%u bytes=%u no_control_writes=1\n",
  Saved,Status,(UINT32)mDvfsJournalPosition));
 if(!Status && Saved){
  UINT64 Pmu[9]={0};UINT32 PmuStatus;CHAR8 Line[192];
  DEBUG((DEBUG_ERROR,"DEGAS DVFS PMU read-only begin no PMU or frequency writes\n"));
  PmuStatus=DegasDvfsPmuRead(Pmu);
  AsciiSPrint(Line,sizeof(Line),"PMU1 status=%u dfr=%016Lx cr=%016Lx en=%016Lx filter=%016Lx\n",PmuStatus,Pmu[0],Pmu[1],Pmu[2],Pmu[3]);
  DvfsEmit(NULL,Line);
  AsciiSPrint(Line,sizeof(Line),"PMU1 refhz=%Lu t0=%016Lx c0=%016Lx t1=%016Lx c1=%016Lx\n",Pmu[4],Pmu[5],Pmu[6],Pmu[7],Pmu[8]);
  DvfsEmit(NULL,Line);Saved=DvfsPublish();
  if(Saved){Status=CpuDvfsClockStep(&S);if(Status==5){Label(Gop,1690,"CLOCK RESTORE FAILED RESET");CpuDeadLoop();}}
 }
 Label(Gop,1690,!Saved?"DVFS JOURNAL SAVE FAILED":Status?"CLOCK TEST STOPPED SEE LOG":"CLOCK STEP AND RESTORE PASS");
 for(D=0;D<3;D++){
  UINT32 Slot=1+D*36;
  if(DegasDvfsSlotValid(&S,Slot+34)){
   AsciiSPrint(Text,sizeof(Text),"D%u REQ %u READY %x",D,S.Raw[Slot+33],S.Raw[Slot+34]);
   Label(Gop,1780+D*80,Text);
  }
 }
 // Return to the same menu, which reloads the watchdog. Boot Android directly
 // from this session; another UEFI launch can overwrite the console ring.
}
