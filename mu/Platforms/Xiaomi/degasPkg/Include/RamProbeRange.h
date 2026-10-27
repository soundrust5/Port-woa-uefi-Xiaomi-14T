// Sparse access/restoration PASS in degas-ram-access-v1. Now allocator validation.
// Selected from 2026-10-25 current-capture-comparison.json. Also outside the
// complete alloc-ranges of all four dynamic reserved-memory nodes in the FDT.
#define DEGAS_RAM_PROBE_BASE   0x140000000ULL
#define DEGAS_RAM_PROBE_SIZE   0x40000000ULL
#define DEGAS_RAM_PROBE_STRIDE 0x200000ULL
#define DEGAS_RAM_PROBE_PAGE   4096U
#define DEGAS_RAM_PROBE_COUNT (DEGAS_RAM_PROBE_SIZE / DEGAS_RAM_PROBE_STRIDE + 1)
