/* Fresh LK entry only. No UEFI-to-Linux direct handoff. */
.text
.balign 4
.global DegasAndroidRoute, DegasAndroidTrampoline, DegasAndroidTrampolineEnd
.global DegasAndroidNormal
DegasAndroidRoute:
 movz x4,#0xef00
 movk x4,#0x480a,lsl #16
 ldp x5,x6,[x4]
 /* Two 64-byte records in unused tail of the already reserved journal page.
  * R=latest entry, M=latest matching request; preserve M across a later normal
  * boot so an Android kernel reset cannot erase evidence of its handoff. */
 add x9,x4,#0x20
 movz x10,#0x4744
 movk x10,#0x5241,lsl #16
 movk x10,#0x554f,lsl #32
 movk x10,#0x3154,lsl #48
 stp x10,xzr,[x9]
 stp x5,x6,[x9,#16]
 mrs x11,CurrentEL
 mov x12,xzr
 cmp x11,#4
 b.ne 8f
 mrs x12,sctlr_el1
8:
 stp x11,x12,[x9,#32]
 dsb sy
 movz x7,#0x4744
 movk x7,#0x4e41,lsl #16
 movk x7,#0x5244,lsl #32
 movk x7,#0x3130,lsl #48
 /* AEE/LK may overwrite the separate AP journal page. The console itself
  * survives these failing boots. Also accept a final magic/inverse token in
  * its validated ring; byte accesses handle unaligned/wrapped tokens. */
 mov x13,#1
 str x13,[x9,#56]
 movz x18,#0xf000
 movk x18,#0x480a,lsl #16
 ldr w19,[x18]
 movz w20,#0x4244
 movk w20,#0x4347,lsl #16
 cmp w19,w20
 b.ne 16f
 ldr w20,[x18,#4]
 ldr w21,[x18,#8]
 movz x19,#0xfff4
 movk x19,#3,lsl #16
 cmp x20,x19
 b.hi 16f
 cmp x21,x19
 b.hi 16f
 cmp x20,x21
 b.hi 16f
 cmp x21,#16
 b.lo 16f
 cmp x20,#16
 b.hs 10f
 add x20,x20,x19
10: sub x20,x20,#16
 mov x26,x20
 add x18,x18,#12
 mov x14,xzr
 mov x15,xzr
 mov x22,xzr
11: ldrb w23,[x18,x20]
 and x24,x22,#7
 lsl x24,x24,#3
 lsl x23,x23,x24
 cmp x22,#8
 b.hs 12f
 orr x14,x14,x23
 b 13f
12: orr x15,x15,x23
13: add x20,x20,#1
 cmp x20,x19
 b.ne 14f
 mov x20,xzr
14: add x22,x22,#1
 cmp x22,#16
 b.lo 11b
 cmp x14,x7
 b.ne 16f
 mvn x25,x7
 cmp x15,x25
 b.ne 16f
 mov x5,x14
 mov x6,x15
 mov x13,#2
 str x13,[x9,#56]
 mov x22,#16
15: strb wzr,[x18,x26]
 add x26,x26,#1
 cmp x26,x19
 b.ne 17f
 mov x26,xzr
17: subs x22,x22,#1
 b.ne 15b
 dsb sy
16:
 cmp x5,x7
 b.ne 7f
 mvn x7,x7
 cmp x6,x7
 b.ne 7f
 stp xzr,xzr,[x4]                 /* Consume once, including guard failure. */
 dsb sy
 mrs x5,CurrentEL
 cmp x5,#4
 b.ne 6f
 mrs x5,sctlr_el1
 tbnz x5,#0,5f
 tbnz x5,#2,5f
 tbnz x5,#25,5f
 mov x13,#5                     /* qualified jump to stock Image */
 str x13,[x9,#8]
 ldp x10,x11,[x9]
 stp x10,x11,[x9,#64]
 ldp x10,x11,[x9,#16]
 stp x10,x11,[x9,#80]
 ldp x10,x11,[x9,#32]
 stp x10,x11,[x9,#96]
 ldp x10,x11,[x9,#48]
 stp x10,x11,[x9,#112]
 dsb sy
 mrs x17,daif
 msr daifset,#15
 adr x4,DegasAndroidTrampoline
 adr x5,DegasAndroidTrampolineEnd
 movz x6,#0
 movk x6,#0x4447,lsl #16
 mov x7,x6
1: ldr w8,[x4],#4
 str w8,[x7],#4
 cmp x4,x5
 b.lo 1b
 dsb sy
 ic iallu
 dsb sy
 isb
 br x6
7: mov x13,#6                   /* missing/torn request */
 str x13,[x9,#8]
 dsb sy
 b 9f
6: mov x13,#3                   /* EL guard rejected a matching request */
 b 4f
5: mov x13,#4                   /* MMU/cache/endianness guard */
4: str x13,[x9,#8]
 ldp x10,x11,[x9]
 stp x10,x11,[x9,#64]
 ldp x10,x11,[x9,#16]
 stp x10,x11,[x9,#80]
 ldp x10,x11,[x9,#32]
 stp x10,x11,[x9,#96]
 ldp x10,x11,[x9,#48]
 stp x10,x11,[x9,#112]
 dsb sy
9:
DegasAndroidNormal:
 movz x4,#0x40
 movk x4,#0x4000,lsl #16
 br x4

/* All operands physical; destination precedes overlapping source. */
DegasAndroidTrampoline:
 movz x4,#0
#if DEGAS_ANDROID_STOCK_HIGH
 movk x4,#0x4220,lsl #16
#else
 movk x4,#0x4040,lsl #16
#endif
 movz x5,#0
 movk x5,#0x4000,lsl #16
 movz x6,#0
 movk x6,#0x227,lsl #16
2: ldp x7,x8,[x4],#16
 stp x7,x8,[x5],#16
 subs x6,x6,#16
 b.ne 2b
 dsb sy
 ic iallu
 dsb sy
 isb
 msr daif,x17
 movz x4,#0
 movk x4,#0x4000,lsl #16
 br x4
DegasAndroidTrampolineEnd:
