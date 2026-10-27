#ifndef DEGAS_GCE_ARENA_H
#define DEGAS_GCE_ARENA_H
// Loader-owned RAM; excluded from DXE allocations and retained until reset.
#define DEGAS_GCE_ARENA_BASE 0x44440000ULL
#define DEGAS_GCE_ARENA_SIZE 0x00030000ULL
#define DEGAS_GCE_ROOT_PA    0x44440000ULL
#define DEGAS_GCE_LEAF_PA    0x44450000ULL
#define DEGAS_GCE_PACKET_PA  0x44451000ULL
// Diagnostic identity IOVA: both translated and physical fetch hypotheses
// point at owned RAM, never at an unrelated MMIO address. Does not prove mode.
#define DEGAS_GCE_IOVA       DEGAS_GCE_PACKET_PA
#define DEGAS_GCE_MAP_BASE   (DEGAS_GCE_IOVA & ~0xfffffULL)
#define DEGAS_GCE_PAGE_INDEX ((DEGAS_GCE_IOVA >> 12) & 255U)
#define DEGAS_GCE_ROOT_WORDS 16384U
#define DEGAS_GCE_LEAF_WORDS 256U
#endif
