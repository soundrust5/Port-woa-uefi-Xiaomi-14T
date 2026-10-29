# AP1 physical-observer result, descriptor v2

User again reported HAL. Fresh CRC-valid retained AP1 record:
`evidence/raw/2026-10-28-degas-aplog-descriptor-result/decoded.json`.

Probe stage2, status3: invalid descriptor while translating the PCR-derived
Number field. Translation level2; descriptor PA `0x14deb0018`; raw value0.
Saved TPIDR `FFFFCB00C0681000`; LPB `FFFFF8012C1795A0`;
physical-view LPB.PRCB `FFFFF8012D060980`;
PC `FFFFF80138291BA0`; TTBR1 `004000004052A800`.

Both audits confirmed the source offsets and the Windows saved-state TTBR1
restore sequence. The reader reached an allowed high-RAM table; it did not
reject that region as outside its whitelist. This is not evidence of NULL
queues or of a faulty Windows translation root: MMU-off physical observations
may not see dirty Normal-WB table/data cache lines.

Captured installed hal.dll is a forwarder; HalExtCCN510/PL080 and acpi.sys scan
did not locate the reported exact HAL tuple. The acpi.sys literal0x5c sites
call a logging helper, not a bugcheck. No resource/clock fix is justified.

Next isolated control: a BSP-owned WB page with known baseline, dirty update
without explicit cache cleaning, and post-clean update, sampled by a single
MMU/cache-off AP through a separate UC handshake page. Old-before/new-after
would demonstrate the physical observer limitation. New in both samples is
inconclusive because eviction can occur; it does not prove coherent visibility.
No Windows context, page-table, scheduler-pointer or live-cache patch is made.
