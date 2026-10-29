// Diagnostic physical reader. PRIVATE register ABI, not a C ABI function.
// Input x0=upper canonical VA, x1=TTBR1_EL1 snapshot, x2=TCR_EL1 snapshot.
// Output x0=status, x1=value, x2=physical address. On failure x1=x2=0.
// Clobbers x3-x17 and NZCV. Preserves x18-x30/SP. No writes or sysreg access.
.text
.balign 16
.global DegasPrcbRead64
.global DegasPrcbRead64End

// Internal shared guard uses X13=base, X12=span; only X14-X16/NZCV
// are scratch. Saving the caller LR in X17 avoids any stack or memory writes.
.macro ram_guard base, span
  mov x13, \base
  mov x12, #\span
  bl .Lram_guard
.endm

DegasPrcbRead64:
  mov x17, x30
  // Exact captured translation regime: 47-bit upper VA, 4K granule,
  // 48-bit PA, no TBI1/LPA2. Do not infer a regime from an arbitrary caller.
  movz x3, #0x3511
  movk x3, #0xb551, lsl #16
  movk x3, #0x0695, lsl #32
  cmp x2, x3
  b.ne .Ltcr
  tst x0, #7
  b.ne .Lva
  lsr x3, x0, #47
  mov x4, #0x1ffff
  cmp x3, x4
  b.ne .Lva
  mov x3, x0
  // ASID (63:48) and CnP(0) are not PA bits. Bits10:1 must be zero.
  tst x1, #0x7fe
  b.ne .Lttbr
  and x4, x1, #0x0000fffffffff800
  ram_guard x4, 2048
  mov x5, #0
  mov x6, #39
.Ltable:
  lsrv x9, x3, x6
  cbnz x5, .Lfull_index
  and x9, x9, #0xff
  b .Lindex_done
.Lfull_index:
  and x9, x9, #0x1ff
.Lindex_done:
  add x9, x4, x9, lsl #3
  ram_guard x9, 8
  ldr x7, [x9]
.ifdef DEGAS_OBSERVER_COHERENT
  // Targeted diagnostic maintenance AFTER observing the coherent value.
  // This may publish BSP dirty state; it never changes the sampled value.
  dc civac, x9
  dsb sy
.endif
  tbz x7, #0, .Linvalid
  // 48-bit PA without LPA2: descriptor bits49:48 are reserved.
  tst x7, #0x0003000000000000
  b.ne .Lreserved
  and x8, x7, #3
  cmp x5, #3
  b.eq .Lpage
  cmp x8, #1
  b.eq .Lblock
  // Table descriptor bits51:48 must be zero in this regime.
  tst x7, #0x000f000000000000
  b.ne .Lreserved
  and x4, x7, #0x0000fffffffff000
  ram_guard x4, 4096
  add x5, x5, #1
  sub x6, x6, #9
  b .Ltable
.Lblock:
  cbz x5, .Lreserved
  cmp x5, #1
  b.eq .Lblock1
  // Level2 block: address bits20:12 are reserved.
  tst x7, #0x1ff000
  b.ne .Lreserved
  and x2, x7, #0x0000ffffffe00000
  and x11, x3, #0x1fffff
  b .Lleaf
.Lblock1:
  // Level1 block: address bits29:12 are reserved.
  tst x7, #0x3ffff000
  b.ne .Lreserved
  and x2, x7, #0x0000ffffc0000000
  and x11, x3, #0x3fffffff
  b .Lleaf
.Lpage:
  cmp x8, #3
  b.ne .Lreserved
  and x2, x7, #0x0000fffffffff000
  and x11, x3, #0xfff
.Lleaf:
  orr x2, x2, x11
  ram_guard x2, 8
  ldr x1, [x2]
.ifdef DEGAS_OBSERVER_COHERENT
  dc civac, x2
  dsb sy
.endif
  mov x0, #0
  mov x30, x17
  ret
.Lva:
  mov x0, #1
  b .Lfail
.Lttbr:
  mov x0, #2
  b .Lfail
.Linvalid:
  mov x0, #3
  b .Lfail
.Lreserved:
  mov x0, #4
  b .Lfail
.Lrange:
  mov x0, #5
  b .Lfail
.Ltcr:
  mov x0, #6
.Lfail:
  mov x1, #0
  mov x2, #0
  mov x30, x17
  ret
.Lram_guard:
  adds x14, x13, x12
  b.cs .Lrange
  mov x15, #0x40480000
  cmp x13, x15
  b.lo 91f
  mov x16, #0x44440000
  cmp x14, x16
  b.ls 94f
91:
  movz x15, #0x4000, lsl #16
  movk x15, #1, lsl #32
  cmp x13, x15
  b.lo 92f
  movz x16, #0xe000
  movk x16, #0xbfff, lsl #16
  movk x16, #1, lsl #32
  cmp x14, x16
  b.ls 94f
92:
  mov x15, #0x1c0000000
  cmp x13, x15
  b.lo .Lrange
  movz x16, #0
  movk x16, #0x2d40, lsl #16
  movk x16, #3, lsl #32
  cmp x14, x16
  b.hi .Lrange
94:
  ret
DegasPrcbRead64End:
