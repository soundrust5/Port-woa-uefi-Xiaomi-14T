#ifndef DEGAS_THERMAL_H
#define DEGAS_THERMAL_H
#include <ntddk.h>

typedef struct _DEGAS_THERMAL_TELEMETRY {
    LONG CoreTemperatureMilliC[8]; /* Raw; may be stale even if plausible. */
    ULONG CeilingKhz[3];
    LONG LimitOpp[3];
    ULONG UtcCount; /* No established liveness semantics. */
    BOOLEAN FreshnessQualified; /* Runtime liveness guard; no firmware API promise. */
} DEGAS_THERMAL_TELEMETRY;

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS DegasThermalInitialize(VOID);
/* Caller must quiesce reads before shutdown. No unload while registered. */
_IRQL_requires_(PASSIVE_LEVEL)
VOID DegasThermalShutdown(VOID);
/* Single backend worker, normally every200ms. Reads MMIO outside cache lock. */
_IRQL_requires_(PASSIVE_LEVEL)
VOID DegasThermalPoll(VOID);
_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN DegasThermalRead(_In_ ULONG Domain, _Out_ ULONG *CeilingKhz,
                        _Out_ LONG *TemperatureMilliC);
/* Cached telemetry. TRUE means a snapshot exists; use DegasThermalRead for caps. */
_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN DegasThermalReadTelemetry(_Out_ DEGAS_THERMAL_TELEMETRY *Snapshot);
#endif
