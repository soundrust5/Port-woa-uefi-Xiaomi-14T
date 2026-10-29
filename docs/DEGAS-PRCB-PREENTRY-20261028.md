# Guarded pre-entry PRCB observation

2026-10-28. The installed kernel SHA-256 matches the audited copy exactly. The phone is in Android while the diagnostic image is prepared.

The AP1 observer uses the saved Windows TTBR1/TCR to perform a bounded, read-only software walk. It accepts only the captured 47-bit, 4KiB regime, preserves the 2KiB root half, and validates every physical read against the three ordinary RAM intervals in the platform map. It does not access MMIO, change page tables, modify OS mailbox data or change architectural registers.

Screen values, left column top-to-bottom then right:
1. High32 = failed stage, low32 = reader status; zero means every read succeeded.
2. Restored X0 / loader-block VA.
3. PRCB read from loader-block+0x88.
4. Processor number, extracted from upper32bits of aligned PRCB+0x20 read.
5. PRCB+0x8300 local ready-queue pointer.
6. PRCB+0x1908 shared ready-queue pointer.
7. Restored PC.
8. Saved TTBR1.

Stages:1 loader-block PRCB read,2 processor number,3 local queue,4 shared queue.
Reader errors:1 VA/alignment,2 TTBR format,3 invalid descriptor,4 reserved encoding,5 physical whitelist,6 TCR regime.

Layout: firmware code at page+0x800, <=0x600 bytes, ending no later than+0xE00. Snapshot+0xE00..0xE3F, telemetry+0xF00..0xF3F; OS-owned16..2047 untouched. Reviewed code1444bytes leaves92bytes before the code limit.

Validation:159 standalone-reader cases,14 integrated AP cases,105 explicitly historical marker/handshake cases. Integrated checks include physical read guards, no OS/table writes, bounded failures, handoff register preservation, exact rendered pixels and layout separation.

Interpretation limit: these are pre-entry physical reads with the AP MMU off, not a Windows debugger memory snapshot. A zero result does not alone prove the virtual read later performed by Windows observes the same value; cache visibility remains a consideration. This diagnostic is not a demonstrated CPU fix.
