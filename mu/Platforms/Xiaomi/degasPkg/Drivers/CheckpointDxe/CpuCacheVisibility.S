// UEFI-only CPU1 cache-visibility probe. No stack or PSCI context dependency.
.text
.balign 16
.global DegasCacheProbeStart
.global DegasCacheProbeEnd
.global DegasCacheProbeMailbox
DegasCacheProbeStart:
  msr daifset, #15
  ldr x19, DegasCacheProbeMailbox
  mrs x1, mpidr_el1
  str x1, [x19, #0x18]
  mrs x1, sctlr_el1
  str x1, [x19, #0x20]
  mov x2, #0x1005
  tst x1, x2
  b.ne 4f
  mrs x1, CurrentEL
  str x1, [x19, #0x28]
  cmp x1, #4
  b.ne 5f
  ldr x20, [x19, #0x40]
  mov x21, #1
  str x21, [x19, #0x10]
  dsb sy
1:
  mov x22, #0x1000000
2:
  ldr x1, [x19]
  cmp x1, x21
  b.eq 3f
  subs x22, x22, #1
  b.ne 2b
  mov x1, #3
  b 6f
3:
  dsb sy
  ldr x2, [x20]
  add x3, x19, x21, lsl #3
  str x2, [x3, #0x78]
  dsb sy
  str x21, [x19, #8]
  dsb sy
  add x21, x21, #1
  cmp x21, #4
  b.ne 1b
  b 7f
4:
  mov x1, #1
  b 6f
5:
  mov x1, #2
6:
  str x1, [x19, #0x48]
7:
  mov x1, #1
  str x1, [x19, #0x38]
  dsb sy
  movz x0, #2
  movk x0, #0x8400, lsl #16
  smc #0
  str x0, [x19, #0x30]
  mov x1, #4
  str x1, [x19, #0x48]
  dsb sy
8:
  wfe
  b 8b
.balign 8
DegasCacheProbeMailbox:
  .quad 0
DegasCacheProbeEnd:
