// Unintegrated reference writer. No stack, UEFI, MMIO, optional CRC ISA or cache
// operations. Caller establishes physical reachability and supplies owned slot.
// x0=slot, x1=mailbox, x2=initial TPIDR_EL1, x3=LPB VA,
// x4=observed physical AP ID, x5=raw mailbox LocalId, x6=status.
// Preserves x0..x6, x14..x29. Clobbers x7..x13, x30 and flags.
// x13 holds incoming LR across the private bitwise CRC helper.
// Caller must preserve its OS handoff registers and publish only once per AP.
.text
.balign 8
.global DegasApJournalCommit
.global DegasApJournalCommitEnd
.global DegasApJournalCrc32
.global DegasApJournalCrc32End
DegasApJournalCommit:
  mov x13, x30
  add x7, x1, #0xe00
  add x8, x0, #0x40
  mov x9, #8
1:
  ldr x10, [x7], #8
  str x10, [x8], #8
  subs x9, x9, #1
  b.ne 1b
  add x7, x1, #0xf00
  mov x9, #8
2:
  ldr x10, [x7], #8
  str x10, [x8], #8
  subs x9, x9, #1
  b.ne 2b
  stp x2, x3, [x0, #0xc0]
  stp x4, x5, [x0, #0xd0]
  str x6, [x0, #0xe0]
  add x7, x0, #8
  mov x8, #0xe8
  bl DegasApJournalCrc32
  str w9, [x0, #0xf0]
  mvn w10, w9
  str w10, [x0, #0xf4]
  dsb sy
  ldr x9, 3f
  str x9, [x0]
  dsb sy
  ret x13
3:
  .ascii "DGAPS001"
DegasApJournalCommitEnd:
.balign 4
// CRC-32/ISO-HDLC, reflected polynomial 0xedb88320, init/final XOR ffffffff.
// Input x7=data, x8=positive length; output w9. No optional CPU instructions.
DegasApJournalCrc32:
  mov w9, #-1
  mov w12, #0x8320
  movk w12, #0xedb8, lsl #16
4:
  ldrb w10, [x7], #1
  eor w9, w9, w10
  mov w11, #8
5:
  and w10, w9, #1
  neg w10, w10
  and w10, w10, w12
  eor w9, w10, w9, lsr #1
  subs w11, w11, #1
  b.ne 5b
  subs x8, x8, #1
  b.ne 4b
  mvn w9, w9
  ret
DegasApJournalCrc32End:
