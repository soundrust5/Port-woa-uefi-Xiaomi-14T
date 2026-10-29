// Host-tested, read-only capture policy. No MMIO access exists in this file.
// The integration supplies qualification and aligned 32-bit READ callbacks.
#ifndef DEGAS_UEFI_CSRAM_SNAPSHOT_H
#define DEGAS_UEFI_CSRAM_SNAPSHOT_H
#include <Uefi.h>

#define DEGAS_DVFS_CSRAM_BASE 0x0011BC00ULL
#define DEGAS_DVFS_CSRAM_SIZE 0x00001400ULL
#define DEGAS_DVFS_DOMAIN_COUNT 3U
#define DEGAS_DVFS_LUT_COUNT 32U
#define DEGAS_DVFS_DOMAIN_WORDS 36U
#define DEGAS_DVFS_WORD_COUNT 109U
#define DEGAS_DVFS_NO_FAILED_SLOT DEGAS_DVFS_WORD_COUNT

enum {
  DegasDvfsComplete = 0, DegasDvfsBadArgument = 1,
  DegasDvfsQualificationFailed = 2, DegasDvfsReadFailed = 3,
  DegasDvfsBadStage = 4
};
enum {
  DegasDvfsStageDxe = 1, DegasDvfsStageMenu = 2,
  DegasDvfsStageBeforeOs = 3, DegasDvfsStageBeforeEbs = 4
};

// TRUE must mean this exact aligned word can be read in the current context.
// The qualifier must not perform an MMIO load, alter mappings, or initialize HW.
typedef BOOLEAN (EFIAPI *DEGAS_DVFS_QUALIFY32)(VOID *Context, UINT64 Address);
typedef BOOLEAN (EFIAPI *DEGAS_DVFS_READ32)(VOID *Context, UINT64 Address, UINT32 *Value);
typedef struct {
  UINT32 Stage, Sequence, Status, FailedSlot, QualifiedCount, ReadCount;
  UINT32 Valid[4];
  UINT32 Raw[DEGAS_DVFS_WORD_COUNT];
} DEGAS_DVFS_SNAPSHOT;

STATIC UINT64 DegasDvfsDomainBase(UINT32 Domain) {
  return Domain < DEGAS_DVFS_DOMAIN_COUNT ? 0x0011BC10ULL + (UINT64)Domain * 0x120U : 0;
}

// Slot 0 is global mode. Domain slots are LUT[0..31], enable, request,
// readiness, latency, in that order. Invalid slots never produce an address.
STATIC UINT64 DegasDvfsAddressForSlot(UINT32 Slot) {
  UINT32 Domain, Word;
  UINT64 Base;
  if (Slot == 0) return DEGAS_DVFS_CSRAM_BASE + 0x4CCU;
  if (Slot >= DEGAS_DVFS_WORD_COUNT) return 0;
  --Slot;
  Domain = Slot / DEGAS_DVFS_DOMAIN_WORDS;
  Word = Slot % DEGAS_DVFS_DOMAIN_WORDS;
  Base = DegasDvfsDomainBase(Domain);
  if (Word < DEGAS_DVFS_LUT_COUNT) return Base + Word * 4U;
  if (Word == 32) return Base + 0x84U;
  if (Word == 33) return Base + 0x88U;
  if (Word == 34) return Base + 0x8CU;
  return Base + 0x114U;
}

STATIC BOOLEAN DegasDvfsAddressAllowed(UINT64 Address) {
  UINT32 Slot;
  if ((Address & 3U) != 0) return FALSE;
  // Check the exact DT resource before checking the narrower word allowlist.
  if (Address < DEGAS_DVFS_CSRAM_BASE ||
      Address > DEGAS_DVFS_CSRAM_BASE + DEGAS_DVFS_CSRAM_SIZE - 4U) return FALSE;
  for (Slot = 0; Slot < DEGAS_DVFS_WORD_COUNT; ++Slot)
    if (Address == DegasDvfsAddressForSlot(Slot)) return TRUE;
  return FALSE;
}

STATIC BOOLEAN DegasDvfsSlotValid(CONST DEGAS_DVFS_SNAPSHOT *Snapshot, UINT32 Slot) {
  return Snapshot != NULL && Slot < DEGAS_DVFS_WORD_COUNT &&
    (Snapshot->Valid[Slot / 32U] & (1U << (Slot % 32U))) != 0;
}

// No allocation, Boot Services, CPU_ON, IPI, SMC, register writes, or polling.
// All qualifications must pass before the first read. Read failure aborts
// immediately, retains only earlier successful words, and leaves invalid raw
// slots zero. A callback returning FALSE must not leave a value published.
STATIC UINT32 DegasDvfsCaptureUefi(
  DEGAS_DVFS_QUALIFY32 Qualify, DEGAS_DVFS_READ32 Read,
  VOID *Context, UINT32 Stage, UINT32 Sequence, DEGAS_DVFS_SNAPSHOT *Snapshot
  ) {
  UINT32 Slot, Value;
  UINT64 Address;
  if (Snapshot == NULL) return DegasDvfsBadArgument;
  Snapshot->Stage = Stage;
  Snapshot->Sequence = Sequence;
  Snapshot->Status = DegasDvfsBadArgument;
  Snapshot->FailedSlot = DEGAS_DVFS_NO_FAILED_SLOT;
  Snapshot->QualifiedCount = Snapshot->ReadCount = 0;
  for (Slot = 0; Slot < 4; ++Slot) Snapshot->Valid[Slot] = 0;
  for (Slot = 0; Slot < DEGAS_DVFS_WORD_COUNT; ++Slot) Snapshot->Raw[Slot] = 0;
  if (Qualify == NULL || Read == NULL) return Snapshot->Status;
  if (Stage < DegasDvfsStageDxe || Stage > DegasDvfsStageBeforeEbs) {
    Snapshot->Status = DegasDvfsBadStage;
    return Snapshot->Status;
  }
  for (Slot = 0; Slot < DEGAS_DVFS_WORD_COUNT; ++Slot) {
    Address = DegasDvfsAddressForSlot(Slot);
    if (!DegasDvfsAddressAllowed(Address) || !Qualify(Context, Address)) {
      Snapshot->Status = DegasDvfsQualificationFailed;
      Snapshot->FailedSlot = Slot;
      return Snapshot->Status;
    }
    ++Snapshot->QualifiedCount;
  }
  for (Slot = 0; Slot < DEGAS_DVFS_WORD_COUNT; ++Slot) {
    Address = DegasDvfsAddressForSlot(Slot);
    Value = 0;
    if (!DegasDvfsAddressAllowed(Address) || !Read(Context, Address, &Value)) {
      Snapshot->Status = DegasDvfsReadFailed;
      Snapshot->FailedSlot = Slot;
      return Snapshot->Status;
    }
    Snapshot->Raw[Slot] = Value;
    Snapshot->Valid[Slot / 32U] |= 1U << (Slot % 32U);
    ++Snapshot->ReadCount;
  }
  Snapshot->Status = DegasDvfsComplete;
  return Snapshot->Status;
}
#endif
