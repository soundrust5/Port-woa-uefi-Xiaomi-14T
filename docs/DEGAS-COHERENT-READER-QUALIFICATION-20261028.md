# Coherent-reader qualification before Windows

Hardware qualification completed: `COHERENT READ VERIFIED`, samples A/A/B/B,
status0, phase4, transition4, CPU1 OFF=1, enabled SCTLR=30D00805.
Evidence: `evidence/raw/2026-10-28-degas-coherent-v2-result/console-ramoops-0`.
The observer saw the BSP dirty update before its own post-read CIVAC.

The first cache control returned DIRTY HIDDEN THEN CLEAN VISIBLE on hardware.
This invalidates treating a zero physical table descriptor or old physical
LPB value as proof of the value Windows sees under Normal WB mappings.

The qualified explicit UEFI-only test uses CPU1 and private NVS page tables. Exactly
three 4KiB pages are mapped: separate NC executable code, Device mailbox, and
RO WB Inner Shareable data. Everything else is invalid. Tables are immutable
and cleaned to PoC before CPU_ON. Translation walks are NC; only local TLB
invalidation is used. The AP remains stackless with IRQ/FIQ/SError masked.

Phases: clean baseline physical A; dirty BSP update physical A or B; same dirty
value read with private MMU+C enabled; final physical read after cleaning. The
coherent sample must equal B before the diagnostic target-line CIVAC occurs.
That CIVAC is an intentional side effect after the read. No set/way operation
is used; initial local-cache state relies on PSCI's startup contract. The AP
restores original SCTLR, TCR, TTBR0, TTBR1 and MAIR before CPU_OFF. No Windows
boot is allowed after this test without a reset.

There is no private synchronous exception vector in this initial qualification;
a transition fault can leave CPU1 stuck. The BSP's bounded wait and existing
hardware watchdog provide recovery. The last transition flag is retained in
the BSP console when available. Emulator tests do not prove hardware MMU or
cache behavior. This is an isolated diagnostic, not a production CPU fix.

Files: CpuObserverMap.h, CpuCoherentProbe.S, CpuCoherentTest.h in CheckpointDxe.
The exact AP code is tested at two physical ranges, and a host ASan/UBSan test
checks only the three intended leaf mappings are reachable. Final firmware
must also byte-match the tested assembly fixture before flashing.
