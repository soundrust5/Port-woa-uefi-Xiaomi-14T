# Windows pre-entry coherent observation

CPU1 now calls a separate NVS observer code page before Windows's entry. Private
NVS translation tables map only the established safe RAM whitelist as RO WB
Inner Shareable XN; observer code is NC RX, all parking pages and the retained
journal are Device RW XN. Tables are published before any CPU_ON.

The four bounded walks use Windows's saved TTBR1/TCR as data, under the private
identity mapping. Each verified PTE/leaf load is followed by targeted CIVAC and
DSB. This intentionally can publish dirty BSP data to PoC and may change the
observed boot behavior; it is not a strictly passive observer or an SMP fix.
No Windows-owned memory is written, and no Windows instructions are patched.

Before returning to Parking the observer restores SCTLR/TCR/TTBR0/TTBR1/MAIR,
X18..X30/SP and handoff X0/X1/X10. Parking then commits the retained record and
branches with M/C/I off. Only CPU1 performs the coherent walks in this image.
The existing 30-second diagnostic watchdog at EBS bounds the boot attempt.

Full integration emulation covers guarded failures, exact journal CRC,
unchanged OS-mailbox and expected execution/cache transitions. Hardware memory
behavior was qualified separately by the UEFI A/A/B/B test; the Windows path
still needs its first hardware capture. A synchronous observer fault remains
watchdog-recovered because this initial observer has no private exception vector.
