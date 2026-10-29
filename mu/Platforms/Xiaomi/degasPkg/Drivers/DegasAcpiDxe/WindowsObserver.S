// AP1 diagnostic only. Private identity translations, no Windows code patch.
// Code Normal-NC RX; mailbox Device RW; verified RAM Normal-WB RO shareable.
// Targeted CIVAC after each coherent load can publish BSP dirty data to PoC.
.text
.balign 16
.global DegasWindowsObserverStart, DegasWindowsObserverEnd
.global DegasWindowsObserverRoot, DegasWindowsObserverTcr, DegasWindowsObserverMair
DegasWindowsObserverStart:
 add x2,x0,#0xf48
 stp x18,x19,[x2]
 stp x20,x21,[x2,#16]
 stp x22,x23,[x2,#32]
 stp x24,x25,[x2,#48]
 stp x26,x27,[x2,#64]
 stp x28,x29,[x2,#80]
 str x30,[x2,#96]
 str x0,[x0,#0xfb0]
 str x1,[x0,#0xfb8]
 str x10,[x0,#0xfc0]
 mov x19,x0
 mrs x2,sctlr_el1
 mov x3,#0x1005
 tst x2,x3
 b.ne .Lunsafe
 tbnz x2,#25,.Lunsafe
 mrs x3,CurrentEL
 cmp x3,#4
 b.ne .Lunsafe
 str x2,[x19,#0xff8]
 mrs x3,ttbr0_el1
 str x3,[x19,#0xfd8]
 mrs x3,ttbr1_el1
 str x3,[x19,#0xfe0]
 mrs x3,tcr_el1
 str x3,[x19,#0xfe8]
 mrs x3,mair_el1
 str x3,[x19,#0xff0]
 ldr x3,DegasWindowsObserverRoot
 ldr x4,DegasWindowsObserverTcr
 ldr x5,DegasWindowsObserverMair
 dsb sy
 msr ttbr0_el1,x3
 msr ttbr1_el1,xzr
 msr tcr_el1,x4
 msr mair_el1,x5
 isb
 tlbi vmalle1
 dsb sy
 isb
 orr x2,x2,#1
 orr x2,x2,#4
 msr sctlr_el1,x2
 isb
 // The four bounded walks below have no writes to cacheable RAM.
  ldr x22, [x19, #0xf0]
  ldr x23, [x19, #0xe0]
#if DEGAS_WINDOWS_IDENTITY_OBSERVER
  // Isolated runtime build identification. No PE pointer is followed: every
  // VA is bounded by PrcbWalk using the saved Windows translation regime.
  // Candidate base alone is not evidence: retain entry instruction, DOS/PE
  // signatures and complete RSDS GUID+age from the audited image's offsets.
  ldr x25,[x19,#0x468]
  str x25,[x19,#0xe08]
  mov x27,#1
  mov x0,x25
  mov x1,x22
  mov x2,x23
  bl DegasPrcbRead64
  cbnz x0,13f
  str x1,[x19,#0xe10]
  movz x26,#0x1ba0
  movk x26,#0xa9,lsl #16
  sub x25,x25,x26
  mov x27,#2
  mov x0,x25
  mov x1,x22
  mov x2,x23
  bl DegasPrcbRead64
  cbnz x0,13f
  str x1,[x19,#0xe18]
  mov x27,#3
  add x0,x25,#0x110
  mov x1,x22
  mov x2,x23
  bl DegasPrcbRead64
  cbnz x0,13f
  str x1,[x19,#0xe20]
  movz x26,#0xdc78
  movk x26,#4,lsl #16
  add x26,x25,x26
  mov x27,#4
  mov x0,x26
  mov x1,x22
  mov x2,x23
  bl DegasPrcbRead64
  cbnz x0,13f
  str x1,[x19,#0xe28]
  mov x27,#5
  add x0,x26,#8
  mov x1,x22
  mov x2,x23
  bl DegasPrcbRead64
  cbnz x0,13f
  str x1,[x19,#0xe30]
  mov x27,#6
  add x0,x26,#16
  mov x1,x22
  mov x2,x23
  bl DegasPrcbRead64
  cbnz x0,13f
  str x1,[x19,#0xe38]
  mov x27,#0
#else
  ldr x24, [x19, #0x368]
  ldr x2, [x19, #0x40]
  str x2, [x19, #0xe08]
  ldr x2, [x19, #0x468]
  str x2, [x19, #0xe30]
  str x22, [x19, #0xe38]
  mov x27, #1
  add x0, x24, #0x88
  mov x1, x22
  mov x2, x23
  bl DegasPrcbRead64
  cbnz x0, 13f
  str x1, [x19, #0xe10]
  // Independently probe the PCR supplied in the OS-owned startup state.
  ldr x25, [x19, #0x40]
  and x25, x25, #0xfffffffffffff000
  add x25, x25, #0x980
  mov x27, #2
  add x0, x25, #0x20
  mov x1, x22
  mov x2, x23
  bl DegasPrcbRead64
  cbnz x0, 13f
  lsr x1, x1, #32
  str x1, [x19, #0xe18]
  mov x27, #3
  mov x0, #0x8300
  add x0, x25, x0
  mov x1, x22
  mov x2, x23
  bl DegasPrcbRead64
  cbnz x0, 13f
  str x1, [x19, #0xe20]
  mov x27, #4
  mov x0, #0x1908
  add x0, x25, x0
  mov x1, x22
  mov x2, x23
  bl DegasPrcbRead64
  cbnz x0, 13f
  str x1, [x19, #0xe28]
  mov x27, #0
#endif
13:
  // Invalid-descriptor observation only: identify exactly which bounded
  // physical read failed. These registers remain valid on reader error3.
  cmp x0, #3
  b.ne 15f
  str x5, [x19, #0xe18]
  str x9, [x19, #0xe20]
  str x7, [x19, #0xe28]
15:
  orr x0, x0, x27, lsl #32
  str x0, [x19, #0xe00]
  dsb sy
  ldr x2,[x19,#0xff8]
  msr sctlr_el1,x2
  isb
  tlbi vmalle1
  dsb sy
  isb
  ldr x2,[x19,#0xfd8]
  msr ttbr0_el1,x2
  ldr x2,[x19,#0xfe0]
  msr ttbr1_el1,x2
  ldr x2,[x19,#0xfe8]
  msr tcr_el1,x2
  ldr x2,[x19,#0xff0]
  msr mair_el1,x2
  isb
  b .Lrestore
.Lunsafe:
  mov x2,#7
  str x2,[x19,#0xe00]
.Lrestore:
  ldr x0,[x19,#0xfb0]
  ldr x1,[x19,#0xfb8]
  ldr x10,[x19,#0xfc0]
  add x2,x19,#0xf48
  ldp x18,x19,[x2]
  ldp x20,x21,[x2,#16]
  ldp x22,x23,[x2,#32]
  ldp x24,x25,[x2,#48]
  ldp x26,x27,[x2,#64]
  ldp x28,x29,[x2,#80]
  ldr x30,[x2,#96]
  ret
.balign 8
DegasWindowsObserverRoot:
 .quad 0
DegasWindowsObserverTcr:
 .quad 0x100803019
DegasWindowsObserverMair:
 .quad 0xff0044
#if DEGAS_WINDOWS_BUGCHECK_MONITOR
.include "BugcheckMonitor.inc"
#else
.set DEGAS_OBSERVER_COHERENT,1
#endif
.include "PrcbWalk.inc"
DegasWindowsObserverEnd:
