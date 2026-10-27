// OnePlus 12 boot-CPU diagnostic; never a runtime protocol.
#ifndef OP12_TIMER_PROBE_H
#define OP12_TIMER_PROBE_H
#include <Uefi.h>
#define OP12_TIMER_PROBE_GUID {0x9a36431b,0xd9f8,0x4bec,{0xa8,0x8a,0x21,0x52,0x3a,0x82,0x01,0x16}}
typedef struct {
  UINT64 Version, VirtualBefore, VirtualAfter, PhysicalCount;
  UINT64 Frequency, Daif, Pmr, Group1, PhysicalCtlBefore, PhysicalCtlAfter;
  UINT64 PhysicalCtlExpired, PhysicalCompareBefore, PhysicalCompareAfter;
  UINT64 PhysicalEnabledBefore, PhysicalEnabledAfter, LastPhysicalSource;
  UINT64 RestoreStatus, VirtualStatus, PhysicalStatus;
} OP12_TIMER_RESULT;
typedef EFI_STATUS (EFIAPI *OP12_TIMER_RUN)(OP12_TIMER_RESULT *Result);
typedef struct { OP12_TIMER_RUN Run; } OP12_TIMER_PROBE;
#endif
