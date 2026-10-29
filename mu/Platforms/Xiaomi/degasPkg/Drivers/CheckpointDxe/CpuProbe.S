// Position-independent PSCI entry: no stack, runtime, MMU or interrupts needed.
// x0 = physical address of one private mailbox, kept allocated until reset.
.text
.balign 16
.global DegasApProbeStart
.global DegasApProbeEnd
.global DegasApProbeMailbox
DegasApProbeStart:
  msr daifset, #15
  // Independent literal avoids trusting context delivery before recording x0.
  ldr x19, DegasApProbeMailbox
  // Save original x0..x7 before using any of them as scratch registers.
  stp x0, x1, [x19, #48]
  stp x2, x3, [x19, #64]
  stp x4, x5, [x19, #80]
  stp x6, x7, [x19, #96]
  // Publish entry before any identification-register access can trap.
  mov x1, #0x10
  str x1, [x19]
  dsb sy
  mrs x1, mpidr_el1
  str x1, [x19, #8]
  mrs x1, midr_el1
  str x1, [x19, #16]
  mrs x1, CurrentEL
  str x1, [x19, #24]
  mrs x1, cntfrq_el0
  str x1, [x19, #32]
  dsb sy
  mov x1, #1
  str x1, [x19]
  dsb sy
  movz x0, #2
  movk x0, #0x8400, lsl #16
  smc #0
  // CPU_OFF must not return; preserve the error if it does.
  str x0, [x19, #40]
  dsb sy
  mov x1, #2
  str x1, [x19]
  dsb sy
1: wfe
  b 1b
.balign 8
DegasApProbeMailbox:
  .quad 0
DegasApProbeEnd:

// Independent CPU_ON call, not using ARM_SMC_ARGS or ArmCallSmc.
// C ABI: x0=target, x1=entry, x2=context, x3=private input-record address.
// x19..x29 are untouched; SMC preserves x30 under SMCCC.
.balign 16
.global DegasCpuOnDirect
DegasCpuOnDirect:
  mov x4, x3
  mov x3, x2
  mov x2, x1
  mov x1, x0
  movz x0, #3
  movk x0, #0xc400, lsl #16
  stp x0, x1, [x4]
  stp x2, x3, [x4, #16]
  // Match the zeroed unused inputs of the library-based call.
  mov x4, xzr
  mov x5, xzr
  mov x6, xzr
  mov x7, xzr
  mov x8, xzr
  mov x9, xzr
  mov x10, xzr
  mov x11, xzr
  mov x12, xzr
  mov x13, xzr
  mov x14, xzr
  mov x15, xzr
  mov x16, xzr
  mov x17, xzr
  dsb sy
  smc #0
  ret
