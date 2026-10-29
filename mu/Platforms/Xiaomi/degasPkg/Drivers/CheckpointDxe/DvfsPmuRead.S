// Read-only BSP observation. Never enables, resets or programs the PMU.
.arch armv8.1-a
.text
.balign 16
.global DegasDvfsPmuRead
DegasDvfsPmuRead:
 mrs x1,id_aa64dfr0_el1
 str x1,[x0]
 ubfx x2,x1,#8,#4
 cbz x2,2f
 cmp x2,#15
 b.eq 2f
 mrs x1,pmcr_el0
 str x1,[x0,#8]
 mrs x1,pmcntenset_el0
 str x1,[x0,#16]
 mrs x1,pmccfiltr_el0
 str x1,[x0,#24]
 mrs x1,cntfrq_el0
 str x1,[x0,#32]
 mov x2,#100
 udiv x3,x1,x2
 cbz x3,2f
 isb
 mrs x1,cntvct_el0
 mrs x2,pmccntr_el0
 stp x1,x2,[x0,#40]
 movz x6,#0x9680
 movk x6,#0x98,lsl#16
1:
 subs x6,x6,#1
 b.eq 2f
 mrs x4,cntvct_el0
 sub x5,x4,x1
 cmp x5,x3
 b.lo 1b
 isb
 mrs x2,pmccntr_el0
 mrs x4,cntvct_el0
 stp x4,x2,[x0,#56]
 mov w0,#0
 ret
2:
 mov w0,#1
 ret
