// GATED diagnostic, included inside the copied Windows observer code page.
// AP2 only: x0=private Page2, x10=0x200, ready already published by Parking.
// Preserves x0/x1/x10/x18..x30 and SP; other GPRs/NZCV are scratch.
// Root MUST assemble DegasPrcbRead64 WITHOUT DEGAS_OBSERVER_COHERENT (no
// CIVAC, including AP1's identity reads), and enforce total copied size<4K.
// Root patches source Page1+F0/E0 with Windows TTBR1/TCR. FD0 is an immutable
// commit-helper pointer with the existing callee-saved register ABI.
// No exception vectors installed: unexpected synchronous faults are covered
// by the experimental boot watchdog, not by this bounded software deadline.
.text
.balign 16
.global DegasBugcheckMonitorStart, DegasBugcheckMonitorEnd

// Check BOTH release predicates; every polling iteration uses this macro.
// CNTVCT is the qualified hardware's 13MHz counter: 60s=780000000 ticks.
.macro bc_poll
  ldr w2,[x19]
  cmp w2,#0x200
  b.ne 991f
  ldr x2,[x19,#8]
  cbnz x2,.Lbc_abort
991:
  mrs x2,cntvct_el0
  sub x2,x2,x21
  movz x3,#0xdb00
  movk x3,#0x2e7d,lsl #16
  cmp x2,x3
  b.hs .Lbc_timeout
.endm

.macro bc_read
  mov x1,x22
  mov x2,x23
  bl DegasPrcbRead64
  cbnz x0,.Lbc_walkfail
.endm

DegasBugcheckMonitorStart:
  // Guard before any physical mailbox write or EL1 translation modification.
  mrs x3,CurrentEL
  cmp x3,#4
  b.ne .Lbc_unguarded_return
  mrs x2,sctlr_el1
  mov x3,#0x1005
  tst x2,x3
  b.ne .Lbc_unguarded_return
  tbnz x2,#25,.Lbc_unguarded_return
  cmp x10,#0x200
  b.ne .Lbc_unguarded_return
  add x3,x0,#0xf48
  stp x18,x19,[x3]
  stp x20,x21,[x3,#16]
  stp x22,x23,[x3,#32]
  stp x24,x25,[x3,#48]
  stp x26,x27,[x3,#64]
  stp x28,x29,[x3,#80]
  str x30,[x3,#96]
  str x0,[x0,#0xfb0]
  str x1,[x0,#0xfb8]
  str x10,[x0,#0xfc0]
  str xzr,[x0,#0xfc8] // private-MMU-enabled flag; FD0 untouched
  str x2,[x0,#0xff8]
  mov x19,x0
  sub x20,x19,#0x1000
  mov x24,#0 // unverified NT base
  mov x25,#0 // first stable sample timestamp
  mrs x21,cntvct_el0
  add x3,x19,#0xe00
  stp xzr,xzr,[x3]
  stp xzr,xzr,[x3,#16]
  stp xzr,xzr,[x3,#32]
  stp xzr,xzr,[x3,#48]
  // BCG2 secondary snapshot; remains zero if its reads never execute.
  stp xzr,xzr,[x3,#64]
  stp xzr,xzr,[x3,#80]
  str xzr,[x3,#96]
.Lbc_wait_identity:
  bc_poll
  movz x3,#0xe100
  movk x3,#0x480a,lsl #16
  ldar x4,[x3]
  ldr x5,.Lbc_magic
  cmp x4,x5
  b.eq .Lbc_identity
  yield
  b .Lbc_wait_identity
.Lbc_identity:
  // Freshness is the root's per-attempt journal clear/publication contract.
  // Check all retained identity qwords, not merely an entry-PC suffix.
  ldr x3,[x20,#0xe00]
  cbnz x3,.Lbc_identityfail
  add x3,x20,#0xe10
  adr x4,.Lbc_identity_words
  mov x5,#6
.Lbc_identity_loop:
  ldr x6,[x3],#8
  ldr x7,[x4],#8
  cmp x6,x7
  b.ne .Lbc_identityfail
  subs x5,x5,#1
  b.ne .Lbc_identity_loop
  ldr x24,[x20,#0xe08]
  movz x3,#0x1ba0
  movk x3,#0xa9,lsl #16
  sub x24,x24,x3
  ldr x22,[x20,#0xf0]
  ldr x23,[x20,#0xe0]
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
  ldr x2,[x19,#0xff8]
  orr x2,x2,#1
  orr x2,x2,#4
  msr sctlr_el1,x2
  isb
  mov x2,#1
  str x2,[x19,#0xfc8]
.Lbc_sample:
  bc_poll
  bl .Lbc_flags
  str x29,[x19,#0xe08]
  str x29,[x19,#0xe70]
  mov w2,w29
  cmp w2,#1
  b.ne .Lbc_notstable
  lsr x2,x29,#32
  and w2,w2,#3
  cmp w2,#3
  b.ne .Lbc_notstable
  mov x26,#0
.Lbc_first_tuple:
  movz x0,#0xb9a0
  movk x0,#0xdb,lsl #16
  add x0,x24,x0
  add x0,x0,x26,lsl #3
  add x27,x26,#3
  bc_read
  add x3,x19,#0xe10
  str x1,[x3,x26,lsl #3]
  add x26,x26,#1
  cmp x26,#5
  b.lo .Lbc_first_tuple
  mrs x2,cntvct_el0
  str x2,[x19,#0xe68]
.Lbc_gap:
  bc_poll
  mrs x2,cntvct_el0
  ldr x3,[x19,#0xe68]
  sub x2,x2,x3
  mov x3,#13000 // at least 1ms between complete tuple snapshots
  cmp x2,x3
  b.hs .Lbc_second_start
  yield
  b .Lbc_gap
.Lbc_second_start:
  mov x26,#0
.Lbc_second_tuple:
  movz x0,#0xb9a0
  movk x0,#0xdb,lsl #16
  add x0,x24,x0
  add x0,x0,x26,lsl #3
  add x27,x26,#8
  bc_read
  add x3,x19,#0xeb0 // scratch B tuple; E40..E60 belong to BCG2 details
  str x1,[x3,x26,lsl #3]
  add x26,x26,#1
  cmp x26,#5
  b.lo .Lbc_second_tuple
  bl .Lbc_flags
  ldr x2,[x19,#0xe70]
  cmp x29,x2
  b.ne .Lbc_notstable
  mov x26,#0
  add x3,x19,#0xe10
  add x4,x19,#0xeb0
.Lbc_compare:
  ldr x5,[x3,x26,lsl #3]
  ldr x6,[x4,x26,lsl #3]
  cmp x5,x6
  b.ne .Lbc_notstable
  add x26,x26,#1
  cmp x26,#5
  b.lo .Lbc_compare
  ldr x2,[x19,#0xe10]
  cbz x2,.Lbc_notstable // zero PARAMETER values remain valid
  mrs x2,cntvct_el0
  cbz x25,.Lbc_newstable
  // Settling applies to one unchanged tuple/flag pair across iterations.
  // A newly stable but different tuple restarts the 10ms settling period.
  ldr x3,[x19,#0xea8]
  cmp x3,x29
  b.ne .Lbc_newstable
  add x3,x19,#0xe10
  add x4,x19,#0xe80
  mov x26,#0
.Lbc_previous:
  ldr x5,[x3,x26,lsl #3]
  ldr x6,[x4,x26,lsl #3]
  cmp x5,x6
  b.ne .Lbc_newstable
  add x26,x26,#1
  cmp x26,#5
  b.lo .Lbc_previous
  b .Lbc_settle
.Lbc_newstable:
  mov x25,x2
  str x29,[x19,#0xea8]
  add x3,x19,#0xe10
  add x4,x19,#0xe80
  mov x26,#0
.Lbc_remember:
  ldr x5,[x3,x26,lsl #3]
  str x5,[x4,x26,lsl #3]
  add x26,x26,#1
  cmp x26,#5
  b.lo .Lbc_remember
.Lbc_settle:
  sub x2,x2,x25
  movz x3,#0xfbd0
  movk x3,#1,lsl #16 // 130000 ticks: 10ms settling after first stable pair
  cmp x2,x3
  b.lo .Lbc_sample
  bc_poll
  // Bounded post-stability detail capture. These five aligned qwords expose
  // the source of deferred HAL problems without altering the primary tuple.
  // Error status3 retains its stage and the already captured primary tuple;
  // the host must mark these secondary details incomplete on a read failure.
  mov x27,#13
  movz x0,#0x51e8
  movk x0,#0xe0,lsl #16
  add x0,x24,x0
  bc_read
  lsr x1,x1,#32
  str x1,[x19,#0xe40] // HalpInterruptLastProblem (U32)
  bc_poll
  mov x27,#14
  movz x0,#0x51f8
  movk x0,#0xe0,lsl #16
  add x0,x24,x0
  bc_read
  mov w1,w1
  str x1,[x19,#0xe48] // HalpInterruptLastProblemStatus (raw U32)
  bc_poll
  mov x27,#15
  movz x0,#0x5218
  movk x0,#0xe0,lsl #16
  add x0,x24,x0
  bc_read
  str x1,[x19,#0xe50] // HalpInterruptLastProblemController
  bc_poll
  mov x27,#16
  movz x0,#0x5228
  movk x0,#0xe0,lsl #16
  add x0,x24,x0
  bc_read
  str x1,[x19,#0xe58] // HalpInterruptLastProblemFile (pointer, not followed)
  bc_poll
  mov x27,#17
  movz x0,#0x5230
  movk x0,#0xe0,lsl #16
  add x0,x24,x0
  bc_read
  lsr x1,x1,#32
  str x1,[x19,#0xe60] // HalpInterruptLastProblemLine (U32)
  mov x0,#0
  b .Lbc_result
.Lbc_notstable:
  mov x25,#0
  yield
  b .Lbc_sample

// Two acquire reads on the translated physical aliases, not Device reads
// with M/C off. Reader may LDR first; LDAR is the value used for both flags.
// Active's aligned qword begins at D97968; the U32 flag is at physical+4.
// Initialized=1 is a CLAIM before tuple stores, never a completion marker.
// Active release CAS publishes tuple stores only for the same first writer.
// Concurrent bugchecks can split these roles; stable pairs are diagnostic
// evidence and cannot make that unsupported transactional guarantee.
.Lbc_flags:
  mov x28,x30
  mov x27,#1
  movz x0,#0x7000
  movk x0,#0xc5,lsl #16
  add x0,x24,x0
  bc_read
  ldar w29,[x2]
  mov x27,#2
  movz x0,#0x7968
  movk x0,#0xd9,lsl #16
  add x0,x24,x0
  bc_read
  add x2,x2,#4
  ldar w3,[x2]
  orr x29,x29,x3,lsl #32
  mov x30,x28
  ret
.Lbc_timeout:
  mov x0,#1
  b .Lbc_result
.Lbc_identityfail:
  mov x0,#2
  b .Lbc_result
.Lbc_walkfail:
  mov x0,#3
  orr x0,x0,x27,lsl #32
.Lbc_result:
  // Recheck OS ownership immediately before committing a diagnostic result.
  ldr w2,[x19]
  cmp w2,#0x200
  b.ne .Lbc_record
  ldr x2,[x19,#8]
  cbnz x2,.Lbc_abort
.Lbc_record:
  str x0,[x19,#0xe00]
  mrs x2,cntvct_el0
  str x2,[x19,#0xe38]
  bl .Lbc_disable
  mrs x2,mpidr_el1
  str x2,[x19,#0xf08]
  ldr x2,[x19,#0xff8]
  str x2,[x19,#0xf10]
  mrs x2,CurrentEL
  str x2,[x19,#0xf18]
  mov x2,#0x200
  str x2,[x19,#0xf20]
  adr x2,DegasBugcheckMonitorStart
  str x2,[x19,#0xf28]
  str xzr,[x19,#0xf30]
  ldr x2,[x19,#0xe40]
  ldr w3,[x19,#0xe48]
  orr x2,x3,x2,lsl #32
  str x2,[x19,#0xf38] // BCG2: problem upper32 / raw status lower32
  // Root skips the later Parking commit only if this helper committed a slot.
  ldr x7,[x19,#0xfd0]
  cbz x7,.Lbc_restore
  ldr w2,[x19]
  cmp w2,#0x200
  b.ne .Lbc_commit
  ldr x2,[x19,#8]
  cbnz x2,.Lbc_restore // MMU already restored; no monitor journal commit
.Lbc_commit:
  movz x0,#0xe200
  movk x0,#0x480a,lsl #16
  mov x1,x19
  mov x2,x24
  // BCG2 context uses diagnostic pointers/line, not physical/local CPU IDs.
  // Journal metadata and F20 retain the monitor's physical/local identity.
  ldr x3,[x19,#0xe50]
  ldr x4,[x19,#0xe58]
  ldr x5,[x19,#0xe60]
  ldr w6,[x19,#0xe00]
  movz x8,#0x4732,lsl #32
  movk x8,#0x4243,lsl #48
  orr x6,x6,x8
  blr x7
  b .Lbc_restore
.Lbc_abort:
  bl .Lbc_disable
  b .Lbc_restore
.Lbc_disable:
  ldr x2,[x19,#0xfc8]
  cbz x2,.Lbc_disabled
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
  str xzr,[x19,#0xfc8]
.Lbc_disabled:
  ret
.Lbc_restore:
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
.Lbc_unguarded_return:
  ret
.balign 8
.Lbc_magic:
  .quad 0x3130305350414744 // DGAPS001
.Lbc_identity_words:
  .quad 0xa9bd53f3d503237f // first two KiSystemStartup instructions
  .quad 0x0000000300905a4d // DOS header
  .quad 0x0022aa6400004550 // PE signature, ARM64 machine, section count
  .quad 0x00d46cc053445352 // RSDS + GUID bytes0..3
  .quad 0x24c2c66a3e48e2a1 // GUID bytes4..11
  .quad 0x0000000123fc9c79 // GUID bytes12..15 + age1
DegasBugcheckMonitorEnd:
