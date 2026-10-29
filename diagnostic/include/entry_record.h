#ifndef DEGAS_ENTRY_RECORD_H
#define DEGAS_ENTRY_RECORD_H

#include <stddef.h>
#include <stdint.h>

/* ABI of entry.S. Valid only with magic "DEGASD01", version 1, stage 1.
 * Register access and record retrieval on real hardware remain unverified.
 */
typedef struct {
    uint8_t magic[8];
    uint32_t version;
    uint32_t size;
    uint64_t stage;
    uint64_t current_el;
    uint64_t original_sp;
    uint64_t original_daif;
    uint64_t sctlr;
    uint64_t tcr;
    uint64_t ttbr0;
    uint64_t mair;
    uint64_t arguments[4];
    uint64_t image_base;
    uint64_t image_size;
    uint8_t build_id[32];
} DegasEntryRecord;

_Static_assert(sizeof(DegasEntryRecord) == 160, "entry record ABI size");
_Static_assert(offsetof(DegasEntryRecord, arguments) == 80, "argument ABI offset");
_Static_assert(offsetof(DegasEntryRecord, build_id) == 128, "build ID ABI offset");

#endif
