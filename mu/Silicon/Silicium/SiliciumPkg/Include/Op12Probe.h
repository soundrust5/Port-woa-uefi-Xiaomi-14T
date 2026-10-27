// SPDX-License-Identifier: BSD-2-Clause-Patent
#ifndef OP12_PROBE_H
#define OP12_PROBE_H
#define OP12_PROBE_HOB_GUID { 0x89d7ef76, 0x5702, 0x4549, {0x9b,0xe4,0x46,0xcf,0x09,0xb8,0x49,0x18} }
#define OP12_PROBE_LOGO_GUID { 0x9ee57b86, 0x0867, 0x439c, {0xa2,0x95,0x7e,0x44,0xf5,0x18,0xc6,0x13} }
typedef VOID (*OP12_PROBE_FINISH)(UINTN, UINTN, UINTN, UINTN, UINTN);
typedef struct {
  UINT64 Magic;
  volatile UINT64 *Record;
  OP12_PROBE_FINISH Finish;
  UINTN StackBase;
  UINTN StackSize;
} OP12_PROBE_HANDOFF;
#define OP12_PROBE_HANDOFF_MAGIC 0x364558443231504fULL
#endif
