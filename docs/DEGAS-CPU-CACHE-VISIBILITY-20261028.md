# Degas CPU cache visibility test — UEFI only

This isolated menu test starts only physical CPU1 through PSCI, independently of
context_id. It does not boot Windows and is not an SMP correction.

Three separately allocated pages hold executable AP code, uncached command/
result words, and an explicitly Normal-WB data line. The BSP requires MMU and
D-cache enabled, records its SCTLR, and initializes/cleans baseline A. The AP
records MPIDR/SCTLR/EL and accepts only EL1 with M/C/I clear.

The same AP performs three ordered reads:
1. A=1122334455667788 after explicit BSP writeback.
2. B=8877665544332211 after a BSP volatile write, with no explicit clean.
3. The SAME B after explicit WriteBackDataCacheRange on that data line.

Between writing B and obtaining the phase2 result the controller does not call
DEBUG, PSCI/SMC or cache maintenance. Results/commands occupy another page.
MicroSecondDelay/MemoryFence polling remains bounded. Environmental eviction or
interrupt activity is possible; no assertion says B must remain resident.

A/A/B demonstrates that the MMU-off AP's physical observation missed a dirty BSP
WB update until writeback. A/B/B is INCONCLUSIVE, since B may have been evicted or
otherwise reached memory without demonstrating a generally coherent read path.
Unexpected baseline/final data or AP errors are not classified as success.

The AP has a bounded command poll and ends in CPU_OFF. The BSP independently
queries affinity OFF. Pages are retained until mandatory watchdog reset; if an
AP is still running, the test dead-loops rather than returning to an OS menu.

Standalone linked AP code: artifacts/degas/cpu-audit/cache-visibility.bin (216B),
SHA2568f08f599a5610a0112cb34afe673c04a1c728dc381b03d9054fb2bcb185ae1a0.
The patched mailbox literal is at0xD0. Tests model backing-memory values and the
protocol/guards/timeouts; they do not emulate hardware cache coherency.
