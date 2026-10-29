/* Ramoops retention experiment; same guarded entry as entry.S. */
.section .text.header, "ax"
.global _start
_start:
    b diagnostic_entry
    .long 0
    .quad 0                         /* Image text_offset; packaging not approved */
    .quad __image_end - _start
    .quad 8                         /* LE, page size unspecified, any 2MiB base */
    .quad 0, 0, 0
    .long 0x644d5241                 /* ARM64 Image magic */
    .long 0

.section .text.entry, "ax"
.global diagnostic_entry, diagnostic_park, diagnostic_unsupported
diagnostic_entry:
    mrs x6, daif
    msr daifset, #15
    mov x5, sp
    mrs x7, CurrentEL
    cmp x7, #4
    b.eq .Lel1
    cmp x7, #8
    b.ne diagnostic_unsupported
    mrs x8, sctlr_el2
    tbnz x8, #0, diagnostic_unsupported
    tbnz x8, #2, diagnostic_unsupported
    tbnz x8, #25, diagnostic_unsupported
    mrs x9, tcr_el2
    mrs x10, ttbr0_el2
    mrs x11, mair_el2
    b .Lcapture
.Lel1:
    mrs x8, sctlr_el1
    tbnz x8, #0, diagnostic_unsupported
    tbnz x8, #2, diagnostic_unsupported
    tbnz x8, #25, diagnostic_unsupported
    mrs x9, tcr_el1
    mrs x10, ttbr0_el1
    mrs x11, mair_el1
.Lcapture:
    adr x4, entry_record
    adr x12, _start
    adr x13, __image_end
    sub x13, x13, x12
    adr x14, __stack_end
    mov sp, x14
    stp x0, x1, [x4, #80]
    stp x2, x3, [x4, #96]
    str x7, [x4, #24]
    stp x5, x6, [x4, #32]
    stp x8, x9, [x4, #48]
    stp x10, x11, [x4, #64]
    stp x12, x13, [x4, #112]
    mov x14, #1
    str x14, [x4, #16]               /* Complete capture stage */
    dsb sy
    movz x14, #0x4544
    movk x14, #0x4147, lsl #16
    movk x14, #0x4453, lsl #32
    movk x14, #0x3130, lsl #48
    str x14, [x4]                    /* "DEGASD01" published last */
    dsb sy
    mov x0, x4                     /* Complete entry record */
    bl diagnostic_ramoops
    dsb sy
    cbnz w0, diagnostic_ramoops_failed
    b diagnostic_ramoops_ok

diagnostic_park:
    wfe
    b diagnostic_park
diagnostic_unsupported:
    wfe
    b diagnostic_unsupported
.global diagnostic_ramoops_ok, diagnostic_ramoops_failed
diagnostic_ramoops_ok:
    wfe
    b diagnostic_ramoops_ok
diagnostic_ramoops_failed:
    wfe
    b diagnostic_ramoops_failed


.section .data.record, "aw"
.balign 16
.global entry_record
entry_record:
    .quad 0                         /* Magic only becomes valid after capture */
    .long 1, 160                    /* ABI version, record size */
    .zero 112                       /* stage through image_size */
#ifndef BUILD_ID_FILE
#define BUILD_ID_FILE "build-ramoops/build-id.bin"
#endif
    .incbin BUILD_ID_FILE            /* 32-byte SHA-256 of probe inputs */

.section .data.stack, "aw"
.balign 16
.global __stack_start, __stack_end
__stack_start:
    .zero 16384                     /* Included in image_size and actual binary */
__stack_end:
