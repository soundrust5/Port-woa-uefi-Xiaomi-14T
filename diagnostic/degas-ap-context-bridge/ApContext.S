// HOST-ONLY PROTOTYPE. Not linked into UEFI or Windows.
// CPU_ON may deliver X0=0. Discover all state independently through MPIDR.
// Entry: EL1, little endian, SCTLR_EL1.{M,C,I}=0; immutable code/config and
// entire READY descriptor already cleaned by publisher to PoC, then DSB SY.
// No stack, calls, MMIO, frequency changes or page table/translation writes.
// X0 is restored; X1..X17 scratch. X18..X30, SP, SIMD and translation state
// are untouched. Successful dispatch restores incoming DAIF exactly.
.text
.balign 4096
.global DegasApContextStart, DegasApContextEnd, DegasApContextConfig
.global DegasApContextPark, DegasApContextClaim
DegasApContextStart:
  mrs x15, daif
  msr daifset, #15
  mrs x0, CurrentEL
  cmp x0, #4
  b.ne DegasApContextPark
  mrs x0, sctlr_el1
  mov x1, #0x1005
  tst x0, x1
  b.ne DegasApContextPark
  tbnz x0, #25, DegasApContextPark

  // Whitelist the confirmed MT6897 affinities: Aff3=Aff2=Aff0=0,
  // Aff1=1..7. Ignore architectural MPIDR U/MT bits, never truncate Aff3.
  mrs x0, mpidr_el1
  movz x1, #0xffff
  movk x1, #0xff, lsl #16
  movk x1, #0xff, lsl #32
  and x0, x0, x1
  tst x0, #0xff
  b.ne DegasApContextPark
  lsr x2, x0, #8
  cmp x2, #1
  b.lo DegasApContextPark
  cmp x2, #7
  b.hi DegasApContextPark

  adr x1, DegasApContextConfig
  ldr x3, [x1]
  movz x4, #0x5231
  movk x4, #0x5842, lsl #16
  movk x4, #0x4354, lsl #32
  movk x4, #0x4150, lsl #48 // CONFIG_MAGIC = 0x4150435458425231
  cmp x3, x4
  b.ne DegasApContextPark
  ldr x7, [x1, #8] // nonzero immutable expected boot epoch
  cbz x7, DegasApContextPark
  ldr x4, [x1, #16] // trusted allocated table PA; also sanity check
  cbz x4, DegasApContextPark
  tst x4, #0xfff
  b.ne DegasApContextPark
  lsr x3, x4, #36
  cbnz x3, DegasApContextPark
  ldr x3, [x4]
  movz x6, #0x4c31
  movk x6, #0x5442, lsl #16
  movk x6, #0x4342, lsl #32
  movk x6, #0x4150, lsl #48 // TABLE_MAGIC = 0x4150434254424c31
  cmp x3, x6
  b.ne DegasApContextPark
  ldr x3, [x4, #8]
  cmp x3, x7
  b.ne DegasApContextPark

  add x5, x4, #128
  add x5, x5, x2, lsl #7 // 128-byte slot per physical CPU
  // LDAR is an acquire load, not an exclusive load. Do not issue exclusive
  // operations against default MMU-off Device data attributes. The caller
  // serializes requests and each physical MPIDR has exactly one consumer.
  ldar x3, [x5]
  cmp x3, #2 // READY
  b.ne DegasApContextPark
  ldr x3, [x5, #8]
  cmp x3, x0 // full canonical affinity
  b.ne DegasApContextPark
  ldr x3, [x5, #16]
  cmp x3, x7
  b.ne DegasApContextPark
  ldr x3, [x5, #40]
  movz x6, #0x5431
  movk x6, #0x534c, lsl #16
  movk x6, #0x4342, lsl #32
  movk x6, #0x4150, lsl #48 // SLOT_MAGIC = 0x41504342534c5431
  cmp x3, x6
  b.ne DegasApContextPark
  ldr x9, [x5, #24]
  cbz x9, DegasApContextPark
  tst x9, #3
  b.ne DegasApContextPark
  ldr x3, [x1, #24] // caller-validated executable physical span
  cmp x9, x3
  b.lo DegasApContextPark
  ldr x3, [x1, #32]
  cmp x9, x3
  b.hs DegasApContextPark
  adr x3, DegasApContextStart
  cmp x9, x3
  b.lo 1f
  add x3, x3, #4096
  cmp x9, x3
  b.lo DegasApContextPark // no recursion into code/config page
1:
  ldr x8, [x5, #32] // zero is legitimate; all 64 bits retained
DegasApContextClaim:
  mov x3, #3 // CLAIMED, one-shot until a proven all-OFF boot epoch reset
  str x3, [x5]
  dsb sy // make the MMU-off store complete before entering Windows
  mov x0, x8
  msr daif, x15
  br x9 // exact native entry; no synthetic success, no startup-state edits

DegasApContextPark:
  wfe
  b DegasApContextPark
.balign 64
DegasApContextConfig:
  // Patched only in a host fixture before publication; immutable once live.
  // magic, expected epoch, table PA, allowed entry low/high (exclusive)
  .quad 0, 0, 0, 0, 0
  .quad 0, 0, 0 // reserved, must be zero
DegasApContextEnd:
