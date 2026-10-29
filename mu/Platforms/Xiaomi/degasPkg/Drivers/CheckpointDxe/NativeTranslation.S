// Read-only qualification of the CURRENT EL1 stage1 identity mapping.
// GetMemoryMap strips actual RP/RO bits in this Mu build, so its descriptors
// establish ownership only. AT checks the real privileged access permissions.
// X0: identity VA; W1: 0=read, nonzero=write; returns PAR_EL1 and restores PAR.
.arch armv8.1-a
.text
.balign 16
.global DegasNativeTranslate
DegasNativeTranslate:
 mrs x3,CurrentEL
 cmp x3,#4
 b.ne 3f
 // S1E1R/W ignore PSTATE.PAN. Refuse that ambiguous access regime.
 mrs x3,pan
 cbnz x3,3f
 mrs x2,par_el1
 cbnz w1,1f
 at s1e1r,x0
 b 2f
1:
 at s1e1w,x0
2:
 isb
 mrs x0,par_el1
 msr par_el1,x2
 isb
 ret
3:
 // A marked precondition failure, not a hardware-generated PAR fault.
 mov x0,#1
 movk x0,#0x8000,lsl #48
 ret
