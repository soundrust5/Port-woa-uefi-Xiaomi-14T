/* Host execution of the same qualifier used by the real AMU probe. */
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef uint64_t ULONGLONG;
typedef uint16_t WCHAR;
#define _KERNEL_MODE
#include "degas_amu.h"
#include "degas_amu_math.h"

static DEGAS_AMU_CPU_STATE sample(void)
{
    DEGAS_AMU_CPU_STATE p;
    memset(&p, 0, sizeof(p));
    p.ReferenceHz = 13000000;
    p.CoreBefore = 100;
    p.CoreAfter = 1300100;
    p.ConstantBefore = 400;
    p.ConstantAfter = 13400;
    p.TimerBefore = 500;
    p.TimerAfter = 13500;
    return p;
}
int main(void)
{
    DEGAS_AMU_CPU_STATE p;
    unsigned i;
    assert(sizeof(DEGAS_AMU_CPU_STATE) == 96);
    assert(sizeof(DEGAS_AMU_STATE) == 784);
    assert(offsetof(DEGAS_DVFS_SNAPSHOT, Amu) == 556);
    assert(sizeof(DEGAS_DVFS_SNAPSHOT) == 1340);
    assert(offsetof(DEGAS_DVFS_SNAPSHOT, DpmPrepareCount) == 280);
    for (i = 0; i < 16; ++i)
        assert(DegasAmuFeatureSupported((ULONGLONG)i << 44) == (i == 1 || i == 2));
    for (i = 0; i < 8; ++i) {
        p = sample(); p.Mpidr = 0x81000000ULL | ((ULONGLONG)i << 8);
        assert(DegasAmuValidateIdentity(&p) && p.Uid == i);
    }
    p.Mpidr = 8u << 8; assert(!DegasAmuValidateIdentity(&p));
    p.Mpidr = 1; assert(!DegasAmuValidateIdentity(&p));
    p.Mpidr = 1u << 16; assert(!DegasAmuValidateIdentity(&p));
    p.Mpidr = 1ULL << 32; assert(!DegasAmuValidateIdentity(&p));
    p = sample(); assert(DegasAmuValidate(&p) == DEGAS_AMU_OK && p.MeasuredKhz == 1300000);
    p = sample(); p.CoreAfter = p.CoreBefore + 3350000;
    assert(DegasAmuValidate(&p) == DEGAS_AMU_OK && p.MeasuredKhz == 3350000);
    p = sample(); p.ReferenceHz = 0; assert(DegasAmuValidate(&p) == DEGAS_AMU_BAD_REFERENCE);
    p = sample(); p.ReferenceHz = 1000000000; assert(DegasAmuValidate(&p) == DEGAS_AMU_BAD_REFERENCE);
    p = sample(); p.CoreAfter = p.CoreBefore; assert(DegasAmuValidate(&p) == DEGAS_AMU_NO_ADVANCE);
    p = sample(); p.ConstantAfter = p.ConstantBefore; assert(DegasAmuValidate(&p) == DEGAS_AMU_NO_ADVANCE);
    p = sample(); p.TimerAfter = p.TimerBefore; assert(DegasAmuValidate(&p) == DEGAS_AMU_NO_ADVANCE);
    p = sample(); p.CoreAfter = 0; assert(DegasAmuValidate(&p) == DEGAS_AMU_NO_ADVANCE);
    p = sample(); p.ConstantAfter = p.ConstantBefore + 10400;
    assert(DegasAmuValidate(&p) == DEGAS_AMU_OK); /* inclusive -20% boundary */
    p.ConstantAfter--; assert(DegasAmuValidate(&p) == DEGAS_AMU_REFERENCE_MISMATCH && !p.MeasuredKhz);
    p = sample(); p.ConstantAfter = p.ConstantBefore + 15600;
    assert(DegasAmuValidate(&p) == DEGAS_AMU_OK); /* inclusive +20% boundary */
    p.ConstantAfter++; assert(DegasAmuValidate(&p) == DEGAS_AMU_REFERENCE_MISMATCH);
    p = sample(); p.ConstantAfter = ~(ULONGLONG)0; assert(DegasAmuValidate(&p) == DEGAS_AMU_REFERENCE_MISMATCH);
    p = sample(); p.CoreAfter = ~(ULONGLONG)0; assert(DegasAmuValidate(&p) == DEGAS_AMU_OVERFLOW);
    p = sample(); p.CoreAfter = p.CoreBefore + 13000; assert(DegasAmuValidate(&p) == DEGAS_AMU_REFERENCE_MISMATCH);
    puts("PASS: actual AMU qualifier math/identity, overflow, zero/wrap/stall, +/-20%, V4 ABI1340/prefix556");
    return 0;
}
