#ifndef DEGAS_AMU_MATH_H
#define DEGAS_AMU_MATH_H
/* Shared portable sample validation; all inputs are read-only hardware data. */
static __inline ULONG DegasAmuFeatureSupported(ULONGLONG Pfr0)
{
    ULONG feature = (ULONG)((Pfr0 >> 44) & 15);
    return feature == 1 || feature == 2; /* AMUv1 / AMUv1p1; reserved fail closed. */
}

static __inline ULONG DegasAmuValidateIdentity(DEGAS_AMU_CPU_STATE *p)
{
    p->Uid = (ULONG)((p->Mpidr >> 8) & 0xff);
    /* Aff0/Aff2/Aff3 must be zero; ignore only non-affinity MPIDR flags. */
    return p->Uid < 8 &&
        (p->Mpidr & 0xff00ffffffULL) == ((ULONGLONG)p->Uid << 8);
}

static __inline ULONG DegasAmuValidate(DEGAS_AMU_CPU_STATE *p)
{
    ULONGLONG core, constant, timer, product, khz;
    const ULONGLONG max = ~(ULONGLONG)0;
    p->MeasuredKhz = 0;
    if (p->ReferenceHz < 12000000 || p->ReferenceHz > 14000000)
        return DEGAS_AMU_BAD_REFERENCE;
    if (p->CoreAfter <= p->CoreBefore || p->ConstantAfter <= p->ConstantBefore ||
        p->TimerAfter <= p->TimerBefore) return DEGAS_AMU_NO_ADVANCE;
    core = p->CoreAfter - p->CoreBefore;
    constant = p->ConstantAfter - p->ConstantBefore;
    timer = p->TimerAfter - p->TimerBefore;
    /* 1 ms active sampling must be bounded; also bounds tolerance arithmetic. */
    if (timer > p->ReferenceHz || constant > p->ReferenceHz ||
        constant * 5 < timer * 4 || constant * 5 > timer * 6)
        return DEGAS_AMU_REFERENCE_MISMATCH;
    if (core > max / p->ReferenceHz) return DEGAS_AMU_OVERFLOW;
    product = core * p->ReferenceHz;
    khz = product / constant / 1000;
    if (khz < 100000 || khz > 6000000) return DEGAS_AMU_REFERENCE_MISMATCH;
    p->MeasuredKhz = (ULONG)khz;
    return DEGAS_AMU_OK;
}
#endif
