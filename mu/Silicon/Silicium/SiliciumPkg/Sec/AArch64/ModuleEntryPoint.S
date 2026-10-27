//
// Copyright (c) 2011 - 2025, Arm Limited. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

#include <AsmMacroLib.h>

#include <Library/ArmLib.h>

//
// SCTRL_EL1 Bits
//
.set CTRL_SPAN_BIT, BIT23

ASM_FUNC (_ModuleEntryPoint)
#if defined (ONEPLUS12_SEC_PROBE) && ONEPLUS12_SEC_PROBE == 1
  // OnePlus 12 stage-one checkpoint. The caller owns x19 (record), x20
  // (continuation), and x21 (cookie). Return before touching platform state.
  MOV64 (x9, 0x314345533231504F)
  cmp   x21, x9
  b.ne  Op12NormalSecEntry
  adr   x9, ASM_PFX (_ModuleEntryPoint)
  str   x9, [x19, #112]
  dsb   sy
  br    x20
Op12NormalSecEntry:
#endif
#if defined (ONEPLUS12_SEC_PROBE) && ONEPLUS12_SEC_PROBE >= 2
#if ONEPLUS12_SEC_PROBE >= 7
  MOV64 (x9, 0x374345533231504F)
#elif ONEPLUS12_SEC_PROBE >= 6
  MOV64 (x9, 0x364345533231504F)
#elif ONEPLUS12_SEC_PROBE >= 5
  MOV64 (x9, 0x354345533231504F)
#elif ONEPLUS12_SEC_PROBE >= 4
  MOV64 (x9, 0x344345533231504F)
#elif ONEPLUS12_SEC_PROBE >= 3
  MOV64 (x9, 0x334345533231504F)
#else
  MOV64 (x9, 0x324345533231504F)
#endif
  cmp   x21, x9
  b.ne  Op12InvalidStageTwo
  mrs   x9, CurrentEL
  cmp   x9, #4
  b.ne  Op12InvalidStageTwo
  adrp  x9, ASM_PFX (Op12ProbeContext)
  add   x9, x9, :lo12:ASM_PFX (Op12ProbeContext)
  stp   x19, x20, [x9]
  str   x22, [x9, #16]
  adr   x9, ASM_PFX (_ModuleEntryPoint)
  str   x9, [x19, #112]
  str   x9, [x22, #16]
  b     Op12StageTwoStart
Op12InvalidStageTwo:
  // This experimental FD requires the wrapper's continuation in x20.
  // A stage/version mismatch aborts without mutating processor state.
  br    x20
Op12StageTwoStart:
#endif
  // Do Platform Specific Initialization
  bl    ASM_PFX (PlatformAssemblyInitialize)

  // Disable SPAN
  EL1_OR_EL2 (x0)
1:mrs   x0, sctlr_el1
  orr   x0, x0, #CTRL_SPAN_BIT
  msr   sctlr_el1, x0
2:nop

  // Enable Floating Point
  bl    ASM_PFX (ArmEnableVFP)

  // Setup Exception Vector
  EL1_OR_EL2 (x0)
1:bl    ASM_PFX (SetupExceptionLevel1)
  b     ASM_PFX (MainEntryPoint)
2:bl    ASM_PFX (SetupExceptionLevel2)
  b     ASM_PFX (MainEntryPoint)

ASM_PFX (MainEntryPoint):
  // Get Stack Details
  MOV64 (x3, FixedPcdGet64 (PcdCPUCoresStackBase))
  MOV32 (x4, FixedPcdGet32 (PcdCPUCorePrimaryStackSize))

  // Set Stack End Address
  add   x5, x3, x4

  // Set Stack Pointer
  mov   sp, x5

  // Clear Stack
  mov   x6, xzr
0:stp   x6, x6, [x3], #16
  cmp   x3, x5
  b.lt  0b

  // Pass Parameters
  // x3 was advanced to StackEnd by the clearing loop.
  sub   x0, x5, x4
  mov   x1, x4

  // Set Jump Location
  ldr   x2, =ASM_PFX (SecEntry)

  // Clear Frame Pointer
  mov   x29, xzr

  // Jump to C Code
  blr   x2

#if defined (ONEPLUS12_SEC_PROBE) && ONEPLUS12_SEC_PROBE >= 3
ASM_FUNC (Op12MmuReturn)
  // x0 callback, x1/x2 wrapper tail; no stores after the last cache clean.
  msr daifset, #15
  mov x15, x0
  mov x16, x1
  mov x17, x2
  mrs x12, ctr_el0
  ubfx x12, x12, #16, #4
  mov x13, #4
  lsl x13, x13, x12
  MOV64 (x9, 0xA7100000)
  MOV64 (x10, 0xA7400000)
1:dc civac, x9
  add x9, x9, x13
  cmp x9, x10
  b.lo 1b
  MOV64 (x9, 0xA760D000)
  MOV64 (x10, 0xA764D000)
2:dc civac, x9
  add x9, x9, x13
  cmp x9, x10
  b.lo 2b
  mov x9, x16
  add x10, x16, x17
3:dc civac, x9
  add x9, x9, x13
  cmp x9, x10
  b.lo 3b
#if ONEPLUS12_SEC_PROBE >= 6
  // GOP touches cacheable display memory outside the owned firmware arena.
  // Retire its lines before the stock kernel can remap/reuse this reservation.
  MOV64 (x9, 0xD5100000)
  MOV64 (x10, 0xD6267000)
4:dc civac, x9
  add x9, x9, x13
  cmp x9, x10
  b.lo 4b
#endif
  dsb sy
  mrs x9, sctlr_el1
  bic x9, x9, #1
  bic x9, x9, #4
  msr sctlr_el1, x9
  isb
  tlbi vmalle1
  dsb sy
  isb
  br x15
#endif
