// HOST-ONLY feasibility fixture. This is not integrated into UEFI or Windows.
// The unavailable integration point must route the AP to DegasContextBridge
// after publishing its immutable MPIDR/context/entry table.
// No stack, MMIO, stores, SMC/HVC, or architectural state changes.
.text
.balign 16
.global DegasContextBridge
DegasContextBridge:
    mrs x9, mpidr_el1
    ldr x10, .Laffinity_mask
    and x9, x9, x10
    adr x11, DegasContextTable
    ldr x12, [x11], #8
    cbz x12, DegasContextBridgeFail
    cmp x12, #8
    b.hi DegasContextBridgeFail
    mov x13, xzr
.Lnext:
    ldr x14, [x11]
    cmp x14, x9
    b.ne .Lskip
    cbnz x13, DegasContextBridgeFail
    mov x13, x11
.Lskip:
    add x11, x11, #24
    subs x12, x12, #1
    b.ne .Lnext
    cbz x13, DegasContextBridgeFail
    ldp x0, x1, [x13, #8]
    cbz x1, DegasContextBridgeFail
    tst x1, #3
    b.ne DegasContextBridgeFail
    br x1
.global DegasContextBridgeFail
DegasContextBridgeFail:
    wfe
    b DegasContextBridgeFail
.balign 8
.Laffinity_mask:
    .quad 0xff00ffffff
.global DegasContextTable
DegasContextTable:
    .quad 8
    // Rows: masked MPIDR, arbitrary 64-bit context, original physical entry.
    .zero 8 * 24
.global DegasContextBridgeEnd
DegasContextBridgeEnd:
