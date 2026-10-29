// BSP-only transport qualification and generic prior-journal recovery.
// One existing 4 KiB dmesg slot; no OS boot, AP start or storage writes.
#include "ApJournalCanary.h"
#include "../../Include/ApJournal.h"
STATIC BOOLEAN DegasBugJournalValid(CONST UINT32 *Words){
 CONST UINT8 *P=(CONST UINT8 *)Words,*B=P+64;
 return ApjGet32(P)==0x43474244U && ApjGet32(P+4)==0x7f4U && ApjGet32(P+8)==0x7f4U &&
  ApjEqual(P+12,mApjKmsg,15) && ApjEqual(B,(CONST UINT8 *)"DGBUG001",8) &&
  ApjGet32(B+8)==1 && ApjGet32(B+12)==256 && ApjCrcValid(B,240);
}
STATIC VOID ApJournalCanaryCheck(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop){
 STATIC UINT32 Previous[DEGAS_APJ_WORDS];UINTN I;BOOLEAN Valid,Dumped=FALSE;
 DegasApJournalRead(Previous);Valid=DegasApJournalHeaderValid(Previous)||DegasBugJournalValid(Previous);
 if(DegasBugJournalValid(Previous)){
  CONST UINT8 *B=(CONST UINT8 *)Previous+64;
  DEBUG((DEBUG_ERROR,"DEGAS BUG JOURNAL crash=%u data_valid=%u code=%lx p1=%lx p2=%lx p3=%lx p4=%lx stage=%u event=%u\n",
   ApjGet32(B+108),ApjGet32(B+80),ApjGet64(B+40),ApjGet64(B+48),ApjGet64(B+56),ApjGet64(B+64),ApjGet64(B+72),ApjGet32(B+132),ApjGet32(B+28)));
 }
 for(I=0;I<DEGAS_APJ_WORDS;I++)if(Previous[I]!=mApJournalCanary[I])break;
 // Recover an AP attempt from any valid namespace/version, not only the fixed
 // transport canary. Never alter the retained slot while inspecting it.
 if(Valid)Dumped=DegasApJournalReportAndDump(Previous);
 DEBUG((DEBUG_ERROR,"DEGAS APJ CANARY previous_boot_exact_match=%u\n",I==1024));
 DEBUG((DEBUG_ERROR,"DEGAS APJ GENERIC previous_valid=%u raw_dump_complete=%u\n",Valid,Dumped));
 Label(Gop,500,Valid?"RAM JOURNAL RETAINED":"RAM JOURNAL NOT FOUND");
}
STATIC inline VOID ApJournalRetentionTest(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop){
 STATIC UINT32 Old[DEGAS_APJ_WORDS];BOOLEAN Ok;
 BootWatchdogArmTenSeconds();
 DegasApJournalRead(Old);
 // Require a complete raw backup in the independent console before overwrite.
 if(!DegasApJournalReportAndDump(Old)){
  Label(Gop,1690,"JOURNAL BACKUP FAILED NO WRITE");
  Label(Gop,1840,"WAIT FOR AUTO RESTART");return;
 }
 Ok=DegasApJournalPublish(mApJournalCanary);
 DEBUG((DEBUG_ERROR,"DEGAS APJ CANARY readback=%u slot=480ae000 no_aps=1\n",Ok));
 Label(Gop,1690,Ok?"RAM JOURNAL SAVED":"RAM JOURNAL READBACK FAILED");
 Label(Gop,1840,"WAIT FOR AUTO RESTART");
 BootWatchdogArmTenSeconds();
}
