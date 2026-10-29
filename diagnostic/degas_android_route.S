/* Fresh LK entry only. No UEFI-to-Linux direct handoff. */
.text
.balign 4
.global DegasAndroidRoute, DegasAndroidTrampoline, DegasAndroidTrampolineEnd
.global DegasAndroidNormal
DegasAndroidRoute:
 movz x4,#0xef00
 movk x4,#0x480a,lsl #16
 ldp x5,x6,[x4]
 movz x7,#0x4744
 movk x7,#0x4e41,lsl #16
 movk x7,#0x5244,lsl #32
 movk x7,#0x3130,lsl #48
 cmp x5,x7
 b.ne 9f
 mvn x7,x7
 cmp x6,x7
 b.ne 9f
 stp xzr,xzr,[x4]                 /* Consume once, including guard failure. */
 dsb sy
 mrs x5,CurrentEL
 cmp x5,#4
 b.ne 9f
 mrs x5,sctlr_el1
 tbnz x5,#0,9f
 tbnz x5,#2,9f
 tbnz x5,#25,9f
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
9:
DegasAndroidNormal:
 movz x4,#0x40
 movk x4,#0x4000,lsl #16
 br x4

/* All operands physical; destination precedes overlapping source. */
DegasAndroidTrampoline:
 movz x4,#0
 movk x4,#0x4040,lsl #16
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
