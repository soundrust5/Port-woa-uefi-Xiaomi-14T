#include "degas_thermal.h"
#include "degas_thermal_policy.h"

/* Saved Degas live DT /therm-intf@114000, reg-name therm_sram, size0x400.
 * Active thermal_interface.ko build-id60a16f5fd2cc864a1f90a4550e3ff92f9b2021d0.
 * This is shared telemetry, not raw LVTS controller MMIO. Never initialize it.
 * No therm_cputcm resource exists in the captured DT.
 */
#define DEGAS_THERMAL_PHYSICAL 0x00114000ULL
#define DEGAS_THERMAL_LENGTH 0x400
#define DEGAS_CPU_CEILING_OFFSET 0x200
#define DEGAS_CPU_LIMIT_OPP_OFFSET 0x260
#define DEGAS_UTC_COUNT_OFFSET 0x27C

static PUCHAR gThermalBase;
static KSPIN_LOCK gThermalLock;
static DEGAS_THERMAL_POLICY gThermalPolicy;
static DEGAS_THERMAL_TELEMETRY gThermalTelemetry;

static ULONGLONG DegasThermalNowMs(VOID)
{
    return KeQueryInterruptTime() / 10000;
}

NTSTATUS DegasThermalInitialize(VOID)
{
    PHYSICAL_ADDRESS physical;
    if (gThermalBase) return STATUS_SUCCESS;
    KeInitializeSpinLock(&gThermalLock);
    RtlZeroMemory(&gThermalPolicy, sizeof(gThermalPolicy));
    RtlZeroMemory(&gThermalTelemetry, sizeof(gThermalTelemetry));
    physical.QuadPart = DEGAS_THERMAL_PHYSICAL;
    gThermalBase = (PUCHAR)MmMapIoSpaceEx(physical, DEGAS_THERMAL_LENGTH,
                                        PAGE_READONLY | PAGE_NOCACHE);
    if (!gThermalBase) return STATUS_INSUFFICIENT_RESOURCES;
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL,
        "DegasDvfs: thermal SRAM mapped read-only; guard closed until all8 cores "
        "demonstrate runtime liveness; empirical Windows producer qualification\n");
    return STATUS_SUCCESS;
}

VOID DegasThermalShutdown(VOID)
{
    if (gThermalBase) {
        MmUnmapIoSpace(gThermalBase, DEGAS_THERMAL_LENGTH);
        gThermalBase = NULL;
    }
}

static BOOLEAN DegasThermalReadRaw(DEGAS_THERMAL_TELEMETRY *Snapshot)
{
    ULONG i;
    PUCHAR base = gThermalBase;
    if (!Snapshot) return FALSE;
    RtlZeroMemory(Snapshot, sizeof(*Snapshot));
    if (!base) return FALSE;
    /* Exactly15 aligned reads, no writes, allocation, wait or polling.
     * The producer does not supply a qualified coherent snapshot protocol.
     * A read can race a firmware update; keep its telemetry-only status.
     */
#if defined(_WIN32)
    __try {
#endif
    for (i = 0; i < 8; ++i)
        Snapshot->CoreTemperatureMilliC[i] =
            (LONG)READ_REGISTER_ULONG((PULONG)(base + 4 * i));
    for (i = 0; i < 3; ++i) {
        Snapshot->CeilingKhz[i] = READ_REGISTER_ULONG(
            (PULONG)(base + DEGAS_CPU_CEILING_OFFSET + 4 * i));
        Snapshot->LimitOpp[i] = (LONG)READ_REGISTER_ULONG(
            (PULONG)(base + DEGAS_CPU_LIMIT_OPP_OFFSET + 4 * i));
    }
    Snapshot->UtcCount = READ_REGISTER_ULONG((PULONG)(base + DEGAS_UTC_COUNT_OFFSET));
    Snapshot->FreshnessQualified = FALSE;
#if defined(_WIN32)
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        RtlZeroMemory(Snapshot, sizeof(*Snapshot));return FALSE;
    }
#endif
    return TRUE;
}

BOOLEAN DegasThermalRead(ULONG Domain, ULONG *CeilingKhz, LONG *TemperatureMilliC)
{
    KIRQL oldIrql;
    unsigned int ceiling;
    int temperature, valid;
    ULONGLONG now;
    if (CeilingKhz) *CeilingKhz = 0;
    if (TemperatureMilliC) *TemperatureMilliC = 0;
    if (!CeilingKhz || !TemperatureMilliC || !gThermalBase) return FALSE;
    now = DegasThermalNowMs();
    KeAcquireSpinLock(&gThermalLock, &oldIrql);
    valid = DegasThermalPolicyRead(&gThermalPolicy, now, Domain, &ceiling, &temperature);
    KeReleaseSpinLock(&gThermalLock, oldIrql);
    if (!valid) return FALSE;
    *CeilingKhz = ceiling;
    *TemperatureMilliC = temperature;
    return TRUE;
}

VOID DegasThermalPoll(VOID)
{
    DEGAS_THERMAL_TELEMETRY raw;
    KIRQL oldIrql;
    int temperatures[8];
    unsigned int ceilings[3], ceiling;
    int temperature;
    ULONG i;
    BOOLEAN rawValid;
    ULONGLONG now;
    if (!gThermalBase) return;
    /* No spinlock around MMIO, no waiting, allocation or hardware writes. */
    rawValid = DegasThermalReadRaw(&raw);
    now = DegasThermalNowMs();
    for (i = 0; i < 8; ++i) temperatures[i] = raw.CoreTemperatureMilliC[i];
    for (i = 0; i < 3; ++i) ceilings[i] = raw.CeilingKhz[i];
    KeAcquireSpinLock(&gThermalLock, &oldIrql);
    if (rawValid) {
        DegasThermalPolicyPoll(&gThermalPolicy, now, temperatures, ceilings);
        raw.FreshnessQualified = TRUE;
        for (i = 0; i < 3; ++i)
            if (!DegasThermalPolicyRead(&gThermalPolicy, now, i, &ceiling, &temperature))
                raw.FreshnessQualified = FALSE;
    } else DegasThermalPolicyInvalidate(&gThermalPolicy);
    gThermalTelemetry = raw;
    KeReleaseSpinLock(&gThermalLock, oldIrql);
}

BOOLEAN DegasThermalReadTelemetry(DEGAS_THERMAL_TELEMETRY *Snapshot)
{
    KIRQL oldIrql;
    BOOLEAN havePoll;
    unsigned int i, ceiling;
    int temperature;
    ULONGLONG now;
    if (!Snapshot) return FALSE;
    RtlZeroMemory(Snapshot, sizeof(*Snapshot));
    if (!gThermalBase) return FALSE;
    now = DegasThermalNowMs();
    KeAcquireSpinLock(&gThermalLock, &oldIrql);
    *Snapshot = gThermalTelemetry;
    havePoll = (BOOLEAN)gThermalPolicy.HavePoll;
    for (i = 0; i < 3; ++i)
        if (!DegasThermalPolicyRead(&gThermalPolicy, now, i, &ceiling, &temperature))
            Snapshot->FreshnessQualified = FALSE;
    KeReleaseSpinLock(&gThermalLock, oldIrql);
    return havePoll;
}
