// Optional compact formatter. The integration owns the journal emitter.
#ifndef DEGAS_UEFI_CSRAM_JOURNAL_H
#define DEGAS_UEFI_CSRAM_JOURNAL_H
#include "UefiCsramSnapshot.h"
#include <Library/PrintLib.h>
typedef VOID (EFIAPI *DEGAS_DVFS_EMIT)(VOID *Context, CONST CHAR8 *Line);

STATIC VOID DegasDvfsJournalUefi(
  CONST DEGAS_DVFS_SNAPSHOT *S, DEGAS_DVFS_EMIT Emit, VOID *Context
  ) {
  CHAR8 Line[192];
  UINT32 D, I, J, Slot;
  UINT64 Mask;
  if (S == NULL || Emit == NULL) return;
  AsciiSPrint(Line, sizeof(Line),
    "DVF1 B src=UEFI stage=%u seq=%08x status=%u qual=%u read=%u failed=%u\n",
    S->Stage, S->Sequence, S->Status, S->QualifiedCount, S->ReadCount, S->FailedSlot);
  Emit(Context, Line);
  AsciiSPrint(Line, sizeof(Line), "DVF1 M ok=%u raw=%08x\n",
    DegasDvfsSlotValid(S, 0) ? 1U : 0U, S->Raw[0]);
  Emit(Context, Line);
  for (D = 0; D < DEGAS_DVFS_DOMAIN_COUNT; ++D) {
    Slot = 1U + D * DEGAS_DVFS_DOMAIN_WORDS;
    Mask = 0;
    for (J = 0; J < DEGAS_DVFS_DOMAIN_WORDS; ++J)
      if (DegasDvfsSlotValid(S, Slot + J)) Mask |= 1ULL << J;
    AsciiSPrint(Line, sizeof(Line),
      "DVF1 D d=%u mask=%09Lx en=%08x req=%08x ready=%08x lat=%08x\n",
      D, Mask, S->Raw[Slot + 32U], S->Raw[Slot + 33U],
      S->Raw[Slot + 34U], S->Raw[Slot + 35U]);
    Emit(Context, Line);
    for (I = 0; I < DEGAS_DVFS_LUT_COUNT; I += 8U) {
      AsciiSPrint(Line, sizeof(Line),
        "DVF1 L d=%u i=%02u raw=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x\n",
        D, I, S->Raw[Slot+I], S->Raw[Slot+I+1], S->Raw[Slot+I+2], S->Raw[Slot+I+3],
        S->Raw[Slot+I+4], S->Raw[Slot+I+5], S->Raw[Slot+I+6], S->Raw[Slot+I+7]);
      Emit(Context, Line);
    }
  }
  AsciiSPrint(Line, sizeof(Line), "DVF1 E seq=%08x\n", S->Sequence);
  Emit(Context, Line);
}
#endif
