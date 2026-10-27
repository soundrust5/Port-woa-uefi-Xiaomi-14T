// Callback-based bounded sparse RAM test; backups survive every ordinary failure.
// Synchronous external aborts are not recoverable here: watchdog and RAM log remain.
#include "../../Include/RamProbeRange.h"
typedef struct {
 VOID *Context;
 VOID (*Read)(VOID *,UINT64,UINT64 *);
 VOID (*Write)(VOID *,UINT64,CONST UINT64 *);
 VOID (*Progress)(VOID *,UINT32,UINT32);
} DEGAS_RAM_IO;
typedef struct {
 UINT32 Error; // 0 pass, 1 pattern mismatch, 2 restore mismatch
 UINT32 FailedPhase;
 UINT64 FailedAddress;
 UINT64 Expected;
 UINT64 Actual;
 UINT32 Restored;
} DEGAS_RAM_RESULT;

STATIC UINT64 RamPageAddress(UINT32 Index){
 return DEGAS_RAM_PROBE_BASE + (Index==DEGAS_RAM_PROBE_COUNT-1 ?
  DEGAS_RAM_PROBE_SIZE-DEGAS_RAM_PROBE_PAGE : (UINT64)Index*DEGAS_RAM_PROBE_STRIDE);
}
STATIC UINT64 RamPattern(UINT64 Address,UINT32 Phase){
 UINT64 Value=Address^0x9e3779b97f4a7c15ULL;
 Value^=Value>>30;Value*=0xbf58476d1ce4e5b9ULL;
 Value^=Value>>27;Value*=0x94d049bb133111ebULL;
 Value^=Value>>31;
 return Phase==1 ? Value : ~Value;
}
STATIC VOID RamProbeRun(CONST DEGAS_RAM_IO *Io,UINT64 *Backup,UINT64 *Work,UINT64 *Read,
                        DEGAS_RAM_RESULT *R){
 UINT32 P,W,Phase;
 CONST UINT32 Words=DEGAS_RAM_PROBE_PAGE/sizeof(UINT64);
 UINT64 Address;
 R->Error=0;R->FailedPhase=0;R->FailedAddress=0;R->Expected=0;R->Actual=0;R->Restored=0;
 for(P=0;P<DEGAS_RAM_PROBE_COUNT;P++){
  Io->Progress(Io->Context,0,P);
  Io->Read(Io->Context,RamPageAddress(P),Backup+P*Words);
 }
 for(Phase=1;Phase<=2;Phase++){
  // Keep all samples written together: address aliases must not silently pass.
  for(P=0;P<DEGAS_RAM_PROBE_COUNT;P++){
   Address=RamPageAddress(P);Io->Progress(Io->Context,Phase,P);
   for(W=0;W<Words;W++)Work[W]=RamPattern(Address+W*sizeof(UINT64),Phase);
   Io->Write(Io->Context,Address,Work);
  }
  for(P=0;P<DEGAS_RAM_PROBE_COUNT;P++){
   Address=RamPageAddress(P);Io->Progress(Io->Context,Phase+2,P);
   Io->Read(Io->Context,Address,Read);
   for(W=0;W<Words;W++){
    UINT64 Expected=RamPattern(Address+W*sizeof(UINT64),Phase);
    if(Read[W]!=Expected){
     R->Error=1;R->FailedPhase=Phase;R->FailedAddress=Address+W*sizeof(UINT64);
     R->Expected=Expected;R->Actual=Read[W];goto Restore;
    }
   }
  }
 }
Restore:
 for(P=0;P<DEGAS_RAM_PROBE_COUNT;P++){
  Io->Progress(Io->Context,5,P);
  Io->Write(Io->Context,RamPageAddress(P),Backup+P*Words);
 }
 R->Restored=1;
 for(P=0;P<DEGAS_RAM_PROBE_COUNT;P++){
  Io->Progress(Io->Context,6,P);
  Io->Read(Io->Context,RamPageAddress(P),Read);
  for(W=0;W<Words;W++)if(Read[W]!=Backup[P*Words+W]){
   R->Restored=0;
   if(R->Error==0){
    R->Error=2;R->FailedPhase=6;R->FailedAddress=RamPageAddress(P)+W*sizeof(UINT64);
    R->Expected=Backup[P*Words+W];R->Actual=Read[W];
   }
  }
 }
}
