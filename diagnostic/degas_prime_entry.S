/* Opt-in fresh-LK boot-core experiment. No frequency or MMU programming.
 * Relocatable at +0x11000. CPU7 never enters UEFI before CPU0 is OFF.
 */
.text
.balign 16
.global DegasPrimeEntry,DegasPrimeApEntry,DegasPrimeRecord,DegasPrimeEnd
.global DegasPrimeFallback,DegasPrimeQuarantine
DegasPrimeEntry:
 adr x19,DegasPrimeRecord
 stp x0,x1,[x19,#0x10]
 stp x2,x3,[x19,#0x20]
 mov x4,sp
 mrs x5,daif
 stp x4,x5,[x19,#0x30]
 mrs x1,mpidr_el1
 str x1,[x19,#0x40]
 ubfx x2,x1,#32,#8
 cbnz x2,DegasPrimeFallback
 and x1,x1,#0xffffff
 cbnz x1,DegasPrimeFallback
 mrs x1,CurrentEL
 str x1,[x19,#0x50]
 cmp x1,#4
 b.ne DegasPrimeFallback
 mrs x1,sctlr_el1
 str x1,[x19,#0x48]
 mov x2,#0x1005
 tst x1,x2
 b.ne DegasPrimeFallback
 tbnz x1,#25,DegasPrimeFallback
 msr daifset,#15
 str xzr,[x19,#8]
 dsb sy
 movz x0,#3
 movk x0,#0xc400,lsl #16
 mov x1,#0x700
 adr x2,DegasPrimeApEntry
 mov x3,#0
 smc #0
 str x0,[x19,#0x58]
 cbnz w0,.Lcheck_ap_off
 mov x20,#0x1000000
.Lwait_ready:
 ldr x1,[x19,#8]
 cmp x1,#1
 b.eq .Lbsp_off
 cbnz x1,.Lcheck_ap_off
 subs x20,x20,#1
 b.ne .Lwait_ready
.Lcheck_ap_off:
 movz x0,#4
 movk x0,#0xc400,lsl #16
 mov x1,#0x700
 mov x2,#0
 smc #0
 cmp w0,#1
 b.eq DegasPrimeFallback
 b DegasPrimeQuarantine
.Lbsp_off:
 dsb sy
 movz x0,#2
 movk x0,#0x8400,lsl #16
 smc #0
 str x0,[x19,#0x80] // A returning CPU_OFF did not shut down the BSP.
 b DegasPrimeQuarantine

DegasPrimeApEntry:
 adr x19,DegasPrimeRecord // PSCI context_id is deliberately unused.
 msr daifset,#15
 mrs x1,mpidr_el1
 str x1,[x19,#0x60]
 ubfx x2,x1,#32,#8
 cbnz x2,.Lap_bad
 and x1,x1,#0xffffff
 cmp x1,#0x700
 b.ne .Lap_bad
 mrs x1,CurrentEL
 str x1,[x19,#0x70]
 cmp x1,#4
 b.ne .Lap_bad
 mrs x1,sctlr_el1
 str x1,[x19,#0x68]
 mov x2,#0x1005
 tst x1,x2
 b.ne .Lap_bad
 tbnz x1,#25,.Lap_bad
 mov x1,#1
 str x1,[x19,#8]
 dsb sy
 mov x20,#0x10000 // Bounded SMC polling, unlike the cheap BSP RAM poll.
.Lap_wait_off:
 movz x0,#4
 movk x0,#0xc400,lsl #16
 mov x1,#0
 mov x2,#0
 smc #0
 str x0,[x19,#0x78]
 cmp w0,#1
 b.eq DegasPrimeFallback // Same saved LK ABI, now on AP7 alone.
 cmp w0,#0
 b.ne DegasPrimeQuarantine
 subs x20,x20,#1
 b.ne .Lap_wait_off
 b DegasPrimeQuarantine
.Lap_bad:
 mov x1,#2
 str x1,[x19,#8]
 dsb sy
 movz x0,#2
 movk x0,#0x8400,lsl #16
 smc #0
 b DegasPrimeQuarantine

DegasPrimeFallback:
 ldp x0,x1,[x19,#0x10]
 ldp x2,x3,[x19,#0x20]
 ldp x4,x5,[x19,#0x30]
 mov sp,x4
 msr daif,x5
 movz x4,#0x40
 movk x4,#0x4000,lsl #16
 dsb sy
 br x4

DegasPrimeQuarantine:
 // Ambiguous ownership must never produce two UEFI boot processors.
 movz x4,#0xa000
 movk x4,#0x1c00,lsl #16
 mov w5,#0x5008
 str w5,[x4,#4]
 ldr w5,[x4]
 and w5,w5,#0xffffff
 mov w6,#0x48
 bic w5,w5,w6
 orr w5,w5,#1
 mov w6,#0x22000000
 orr w5,w5,w6
 str w5,[x4]
 mov w5,#0x1971
 str w5,[x4,#8]
 dsb sy
.Lhalt:
 wfe
 b .Lhalt
.balign 16
DegasPrimeRecord:
 .quad 0x31454d4952504744 // DGPRIME1
 .zero 0x88
DegasPrimeEnd:
