// HOST-TESTED SOURCE ONLY; not added to a firmware module by this directory.
// AP entry ignores incoming X0; caller patches its private PC-relative literal.
// BSP must call DegasCacheCollect, never the CPU_OFF wrapper.
.text
.balign 16
.global DegasCacheApStart, DegasCacheApMailbox, DegasCacheProbeEnd
.global DegasCacheCollect, DegasCacheApPark
DegasCacheApStart:
 mov x16,x0
 ldr x0,DegasCacheApMailbox
 cbz x0,DegasCacheApPark
 tst x0,#7
 b.ne DegasCacheApPark
 str x16,[x0,#152]
 bl DegasCacheCollect
 ldr x1,DegasCacheApMailbox
 // The leaf restored its incoming DAIF. Power-off/failure parking are a
 // separate, non-returning AP lifecycle; keep interrupts masked there.
 msr daifset,#15
 mov x2,#0x3f
 str x2,[x1,#64]
 dsb sy
 mrs x2,CurrentEL
 cmp x2,#4
 b.ne .Lap_unsafe_off
 mrs x2,mpidr_el1
 movz x3,#0xffff
 movk x3,#0xff,lsl #16
 movk x3,#0xff,lsl #32
 and x2,x2,x3
 tst x2,#0xff
 b.ne .Lap_unsafe_off
 lsr x2,x2,#8
 cmp x2,#1
 b.lo .Lap_unsafe_off // NEVER CPU_OFF canonical CPU0
 cmp x2,#7
 b.hi .Lap_unsafe_off
 mov x2,#0x40
 str x2,[x1,#64]
 dsb sy
 movz x0,#2
 movk x0,#0x8400,lsl #16
 smc #0
 // CPU_OFF must not return. Reload PC-relative pointer after the SMC.
 ldr x1,DegasCacheApMailbox
 str x0,[x1,#160]
 mov x2,#9
 str x2,[x1,#72]
 mov x2,#0x41
 str x2,[x1,#64]
 dsb sy
 b DegasCacheApPark
.Lap_unsafe_off:
 mov x2,#10
 str x2,[x1,#72]
 mov x2,#0x42
 str x2,[x1,#64]
 dsb sy
DegasCacheApPark:
 msr daifset,#15
 wfe
 b DegasCacheApPark

// C ABI: X0 = an owned, initialized 512-byte record VA; W0 = status.
// Stackless leaf: X0..X17/NZCV scratch; X18..X30/SP/SIMD untouched.
// Restores the observed CSSELR_EL1 and DAIF on all paths that RETURN.
// Traps are not caught; phase+DSB identifies the last attempted operation.
.balign 16
DegasCacheCollect:
 cbz x0,.Lbad_pointer
 tst x0,#7
 b.ne .Lbad_pointer
 mov x15,x0
 mov x14,xzr // whether original CSSELR has been read
 str xzr,[x15,#168] // invalidate completion before changing any result
 str xzr,[x15,#144]
 str xzr,[x15,#176]
 mov x1,#1
 str x1,[x15,#64]
 str x1,[x15,#72]
 dsb sy
 mov x1,#2
 str x1,[x15,#64]
 dsb sy
 mrs x17,daif
 str x17,[x15,#112]
 msr daifset,#15
 mov x1,#3
 str x1,[x15,#64]
 dsb sy
 mrs x2,CurrentEL
 str x2,[x15,#96]
 cmp x2,#4
 b.ne .Lbad_el

 ldr x1,[x15]
 movz x2,#0x4744
 movk x2,#0x4143,lsl #16
 movk x2,#0x4843,lsl #32
 movk x2,#0x3145,lsl #48 // bytes "DGCACHE1"
 cmp x1,x2
 b.ne .Lbad_header
 ldr x1,[x15,#8]
 cmp x1,#512
 b.ne .Lbad_header
 ldr x1,[x15,#16]
 cbz x1,.Lbad_header
 ldr x1,[x15,#24]
 cmp x1,#0x700
 b.hi .Lbad_header
 tst x1,#0xff
 b.ne .Lbad_header
 ldr x1,[x15,#32]
 cmp x1,#1
 b.hi .Lbad_header

 mov x1,#4
 str x1,[x15,#64]
 dsb sy
 mrs x2,mpidr_el1
 str x2,[x15,#80]
 movz x1,#0xffff
 movk x1,#0xff,lsl #16
 movk x1,#0xff,lsl #32
 and x2,x2,x1
 ldr x1,[x15,#24]
 cmp x2,x1
 b.ne .Lbad_mpidr
 mov x1,#5
 str x1,[x15,#64]
 dsb sy
 mrs x2,midr_el1
 str x2,[x15,#88]
 mov x1,#6
 str x1,[x15,#64]
 dsb sy
 mrs x2,sctlr_el1
 str x2,[x15,#104]
 tbnz x2,#25,.Lbad_state
 ldr x1,[x15,#32]
 cbz x1,1f // BSP's current MMU/cache state remains unchanged
 mov x1,#0x1005
 tst x2,x1
 b.ne .Lbad_state
1:
 mov x1,#7
 str x1,[x15,#64]
 dsb sy
 mrs x7,id_aa64mmfr2_el1
 str x7,[x15,#136]
 ubfx x2,x7,#20,#4
 cmp x2,#1
 b.hi .Lbad_ccidx
 mov x1,#8
 str x1,[x15,#64]
 dsb sy
 mrs x6,clidr_el1
 str x6,[x15,#128]
 mov x1,#9
 str x1,[x15,#64]
 dsb sy
 mrs x16,csselr_el1
 str x16,[x15,#120]
 mov x14,#1
 mov x13,x6
 mov x3,xzr // zero-based level, bounded to 0..6
 mov x9,xzr // number of completed raw entries, bounded to 14
 add x8,x15,#256
.Llevel:
 and x4,x13,#7
 cbz x4,.Lsuccess // ignore every higher Ctype after the first zero
 cmp x4,#4
 b.hi .Lbad_ctype
 lsl x5,x3,#1 // TnD=0, data/unified selector
 cmp x4,#1
 b.ne .Lcapture
 orr x5,x5,#1 // instruction-only cache
.Lcapture:
 str x5,[x15,#192]
 lsl x1,x5,#8
 orr x1,x1,#0x20
 str x1,[x15,#64]
 dsb sy
 msr csselr_el1,x5
 isb
 orr x1,x1,#1 // phase 21, immediately before reading selected CCSIDR
 str x1,[x15,#64]
 dsb sy
 mrs x11,ccsidr_el1
 stp x5,x11,[x8],#16
 add x9,x9,#1
 str x9,[x15,#144]
 bic x1,x1,#1
 orr x1,x1,#2 // phase 22, entry and count written
 str x1,[x15,#64]
 dsb sy
 cmp x4,#3
 b.ne .Lnext_level
 tbnz x5,#0,.Lnext_level
 orr x5,x5,#1 // separate I-cache gets its own independent read
 b .Lcapture
.Lnext_level:
 add x3,x3,#1
 lsr x13,x13,#3
 cmp x3,#7
 b.lo .Llevel
.Lsuccess:
 mov x0,xzr
 b .Lrestore
.Lbad_header:
 mov x0,#2
 b .Lrestore
.Lbad_mpidr:
 mov x0,#3
 b .Lrestore
.Lbad_el:
 mov x0,#4
 b .Lrestore
.Lbad_state:
 mov x0,#5
 b .Lrestore
.Lbad_ccidx:
 str x2,[x15,#176]
 mov x0,#6
 b .Lrestore
.Lbad_ctype:
 lsl x1,x3,#32
 orr x1,x1,x4
 str x1,[x15,#176]
 mov x0,#7
.Lrestore:
 str x0,[x15,#72]
 cbz x14,.Lrestore_daif
 mov x1,#0x30
 str x1,[x15,#64]
 dsb sy
 msr csselr_el1,x16
 isb
 mov x1,#0x31
 str x1,[x15,#64]
 dsb sy
.Lrestore_daif:
 mov x1,#0x32
 str x1,[x15,#64]
 dsb sy
 msr daif,x17
 mov x1,#0x33
 str x1,[x15,#64]
 dsb sy
 movz x1,#0x4f44
 movk x1,#0x454e,lsl #16
 movk x1,#0x4143,lsl #32
 movk x1,#0x4843,lsl #48 // bytes "DONECACH"
 str x1,[x15,#168]
 dsb sy
 ret
.Lbad_pointer:
 mov x0,#2
 ret
.balign 8
DegasCacheApMailbox:
 .quad 0
DegasCacheProbeEnd:
