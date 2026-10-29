// Isolated UEFI CPU1 qualification, never a Windows startup trampoline.
// Requires BSP-validated private identity map: NC code, UC mail, WB RO data.
// No cache set/way operations; exactly one target-line CIVAC AFTER its read.
.text
.balign 16
.global DegasCoherentProbeStart, DegasCoherentProbeEnd, DegasCoherentProbeMailbox
DegasCoherentProbeStart:
 msr daifset,#15
 ldr x19,DegasCoherentProbeMailbox
 mrs x1,mpidr_el1
 str x1,[x19,#0x18]
 mrs x27,sctlr_el1
 str x27,[x19,#0x20]
 mov x2,#0x1005
 tst x27,x2
 b.ne .Lbad_state
 tbnz x27,#25,.Lbad_state
 mrs x1,CurrentEL
 str x1,[x19,#0x28]
 cmp x1,#4
 b.ne .Lbad_el
 mrs x23,ttbr0_el1
 mrs x24,ttbr1_el1
 mrs x25,tcr_el1
 mrs x26,mair_el1
 stp x23,x24,[x19,#0x68]
 str x25,[x19,#0x78]
 stp x26,x27,[x19,#0xa0]
 ldr x20,[x19,#0x40]
 mov x21,#1
 str x21,[x19,#0x10]
 dsb sy
.Lphase:
 mov x22,#0x1000000
.Lwait:
 ldr x1,[x19]
 cmp x1,x21
 b.eq .Lsample
 subs x22,x22,#1
 b.ne .Lwait
 mov x1,#3
 b .Lerror
.Lsample:
 dsb sy
 cmp x21,#3
 b.eq .Lcoherent
 ldr x2,[x20]
 b .Lpublish
.Lcoherent:
 mov x1,#1
 str x1,[x19,#0xb0]
 ldp x4,x5,[x19,#0x50] // root and TCR
 ldr x6,[x19,#0x60]   // MAIR
 dsb sy
 msr ttbr0_el1,x4
 msr ttbr1_el1,xzr
 msr mair_el1,x6
 msr tcr_el1,x5
 isb
 tlbi vmalle1
 dsb sy
 isb
 orr x4,x27,#1
 orr x4,x4,#4
 msr sctlr_el1,x4
 isb
 mov x1,#2
 str x1,[x19,#0xb0]
 mrs x1,sctlr_el1
 str x1,[x19,#0xb8]
 ldr x2,[x20]
 // Never clean target before sampling: that would change the experiment.
 dc civac,x20
 dsb sy
 mov x1,#3
 str x1,[x19,#0xb0]
 msr sctlr_el1,x27
 isb
 tlbi vmalle1
 dsb sy
 isb
 msr ttbr0_el1,x23
 msr ttbr1_el1,x24
 msr mair_el1,x26
 msr tcr_el1,x25
 isb
 mov x1,#4
 str x1,[x19,#0xb0]
.Lpublish:
 add x3,x19,x21,lsl #3
 str x2,[x3,#0x78]
 dsb sy
 str x21,[x19,#8]
 dsb sy
 add x21,x21,#1
 cmp x21,#5
 b.ne .Lphase
 b .Loff
.Lbad_state:
 mov x1,#1
 b .Lerror
.Lbad_el:
 mov x1,#2
.Lerror:
 str x1,[x19,#0x48]
.Loff:
 mov x1,#1
 str x1,[x19,#0x38]
 dsb sy
 movz x0,#2
 movk x0,#0x8400,lsl #16
 smc #0
 str x0,[x19,#0x30]
 mov x1,#4
 str x1,[x19,#0x48]
 dsb sy
.Lpark:
 wfe
 b .Lpark
.balign 8
DegasCoherentProbeMailbox:
 .quad 0
DegasCoherentProbeEnd:
