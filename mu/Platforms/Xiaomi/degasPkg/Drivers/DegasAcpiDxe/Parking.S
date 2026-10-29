// Experimental ACPI ARM MP-startup mailbox. Independent of PSCI context_id.
// Copied at offset0x800 of each private NVS4KiB page. No stack or relocations.
.text
.balign 16
.global DegasParkingStart
.global DegasParkingEnd
// Diagnostic-only native framebuffer border marker. Windows's diagnostic
// viewport begins at x128; these x16..31 pixels remain outside its surface.
// Leaf uses x2..x8 only and preserves x0/x1/w9/w10. BL uses scratch LR;
// the final OS handoff is BR, and its ABI does not consume incoming LR.
// No stack, MMIO, cache or architectural state writes.

DegasParkingStart:
  msr daifset, #15
  adr x0, DegasParkingStart
  sub x0, x0, #0x800
  mrs x1, mpidr_el1
  str x1, [x0, #0xf08]
  // This Windows GICv3 HAL uses compressed MPIDR as its mailbox LocalId,
  // independently of the MADT CPU Interface Number (which remains0).
  // Match the installed HAL's aff3:aff2:aff1:aff0 encoding, removing U/MT.
  lsr x11, x1, #8
  eor w10, w11, w1
  and w10, w10, #0xffffff
  eor w10, w10, w11
  mov w1, #-1
  str w1, [x0]
  dsb sy
  mov w8, #-1
  bl DegasParkingMarker
  mov x1, #1
  str x1, [x0, #0xf00]
  dsb sy
#if DEGAS_WINDOWS_BUGCHECK_MONITOR
  // Diagnostic observer only while this OS-advertised AP is still parked.
  // It returns immediately if Windows requests this AP's normal release.
  cmp w10, #0x200
  b.ne .Lmonitor_skipped
  ldr x2, [x0, #0xf40]
  cbz x2, .Lmonitor_skipped
  blr x2
.Lmonitor_skipped:
#endif
  mov w9, #-1
1:
  // Polling avoids relying on uninitialized per-CPU interrupt delivery.
  yield
  ldr w1, [x0]
  // Accept only this AP's HAL LocalId, not arbitrary non-invalid values.
  cmp w1, w10
  b.eq 3f
  cmn w1, #1
  b.eq 1b
  // Capture a Windows/firmware ID mismatch once per changed value. W9 is
  // preserved by the marker macro; do not continuously rewrite the screen.
  cmp w1, w9
  b.eq 1b
  mov w9, w1
  str x1, [x0, #0xf38]
  mov w8, #0
  movk w8, #0xffff, lsl #16
  bl DegasParkingMarker
  b 1b
3:
  ldr x1, [x0, #8]
  cbz x1, 1b
  // Firmware-owned diagnostics only. Never touch OS reserved bytes16..2047.
  mrs x2, sctlr_el1
  mrs x3, CurrentEL
  ldr w4, [x0]
  add x5, x0, #0xf10
  stp x2, x3, [x5]
  stp x4, x1, [x5, #0x10]
  mov x3, #1
  str x3, [x0, #0xf30]
  // Green when MMU/data cache/instruction cache are all off; amber otherwise.
  mov x3, #0x1005
  tst x2, x3
  mov w8, #0xbf00
  movk w8, #0xffff, lsl #16
  b.ne 2f
  mov w8, #0xff00
  movk w8, #0xff00, lsl #16
2:
  bl DegasParkingMarker
  // Only AP1 uses the qualified private observer; all OS fields stay intact.
  cmp w10, #0x100
  b.ne 8f
  ldr x2, [x0, #0xf40]
  cbz x2, 8f
  blr x2
8:
  // Commit one owned retained slot before handing control to Windows. Use
  // physical accesses only in the verified EL1 MMU/cache-off state. Never
  // append through the shared BSP console ring.
  ldr x7, [x0, #0xf10]
  mov x8, #0x1005
  tst x7, x8
  b.ne 14f
  ldr x7, [x0, #0xf18]
  cmp x7, #4
  b.ne 14f
#if DEGAS_WINDOWS_BUGCHECK_MONITOR
  // A monitor slot is single-writer/single-commit. Preserve its evidence if
  // Windows later releases AP2; its normal handoff still proceeds unchanged.
  ldr x7, [x0, #0xf08]
  ubfx x7, x7, #8, #8
  mov x8, #0xe000
  movk x8, #0x480a, lsl #16
  add x8, x8, x7, lsl #8
  ldr x7, [x8]
  cbnz x7, 14f
#endif
  mov x19, x0
  mov x20, x1
  mov w21, w10
  mov x1, x0
  ldr x2, [x1, #0x40] // OS-saved startup TPIDR, not the inherited AP register.
  ldr x3, [x1, #0x368]
  ldr x4, [x1, #0xf08]
  ubfx x4, x4, #8, #8
  ldr w5, [x1]
  ldr x6, [x1, #0xe00]
  mov x0, #0xe000
  movk x0, #0x480a, lsl #16
  add x0, x0, x4, lsl #8
  bl DegasApJournalCommit
  mov x0, x19
  mov x1, x20
  mov w10, w21
14:
  // Leave OS-owned bytes16..2047 untouched. x0 is the mailbox physical base.
  str xzr, [x0, #8]
  dsb sy
  br x1
.balign 4
DegasParkingMarker:
  ldr x4, [x0, #0xf08]
  ubfx x4, x4, #8, #8
  lsl x4, x4, #6
  add x4, x4, #512
  mov x3, #4992
  mov x2, #0xf000
  movk x2, #0xfcc2, lsl #16
  madd x2, x4, x3, x2
  add x2, x2, #64
  mov x6, #16
90:
  mov x7, x2
  mov x5, #16
91:
  str w8, [x7], #4
  subs x5, x5, #1
  b.ne 91b
  add x2, x2, x3
  subs x6, x6, #1
  b.ne 90b
  dsb sy
  ret
.include "ApJournalCommit.inc"
DegasParkingEnd:
