#ifndef DEGAS_BACKEND_H
#define DEGAS_BACKEND_H
#include <ntddk.h>

#define DEGAS_DOMAIN_COUNT 3
typedef struct _DEGAS_DOMAIN_LIMITS {
    ULONG LowestKhz;
    ULONG LowestNonlinearKhz;
    ULONG NominalKhz;
    ULONG HighestKhz;
    ULONG TransitionLatency100ns;
    ULONG TransitionOverhead100ns;
} DEGAS_DOMAIN_LIMITS;
typedef struct _DEGAS_BACKEND_INFO {
    DEGAS_DOMAIN_LIMITS Domains[DEGAS_DOMAIN_COUNT];
} DEGAS_BACKEND_INFO;
typedef struct _DEGAS_DOMAIN_SNAPSHOT {
    ULONG RequestKhz;
    ULONG CeilingKhz;
    ULONG LimitReasons; /* PERFORMANCE_LIMIT_* */
    BOOLEAN Ready;
    LONG TemperatureMilliC;
    ULONG ThermalStatus;
    BOOLEAN ThermalValid;
} DEGAS_DOMAIN_SNAPSHOT;

/* Must validate mapping, all controller readiness, kHz mode and limits. Runtime
 * thermal freshness gates raising frequency, not early OS PEP registration.
 * Successful init must arrange independent thermal safety work before enabling
 * requests; a not-yet-fresh guard retains safe low ceilings in ReadSnapshot.
 * On failure it may retain safe mappings for ReadSnapshot; Set remains disabled.
 * ReadSnapshot must be safe even if initialization failed before any mapping. */
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS DegasBackendInitialize(_Out_ DEGAS_BACKEND_INFO *Info);

/* No allocation, sleeping, polling loop, mailbox, or pageable code. Must enforce
 * final hardware/thermal limits itself. FALSE means no transition was accepted.
 * Provider serializes calls per domain at DISPATCH_LEVEL. Select a continuous
 * kHz request within MinimumKhz..MaximumKhz whenever thermal permits. Safety may
 * override MinimumKhz; accept that lowered request with TRUE and report THERMAL
 * in ReadSnapshot, rather than rejecting and leaving the old high request. */
_IRQL_requires_(DISPATCH_LEVEL)
BOOLEAN DegasBackendSetDomainKhz(_In_ ULONG DomainId, _In_ ULONG DesiredKhz,
                               _In_ ULONG MinimumKhz, _In_ ULONG MaximumKhz);

/* Backend safety worker calls after a domain's effective ceiling/reasons change,
 * outside backend locks. Only queues OS notification; never changes hardware. */
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID DegasPepNotifyConstraints(_In_ ULONG DomainId);

/* Bounded nonblocking snapshot; no counter is represented as actual frequency. */
_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN DegasBackendReadSnapshot(_In_ ULONG DomainId,
                                _Out_ DEGAS_DOMAIN_SNAPSHOT *Snapshot);

/* Called only before registration, after initialization succeeded but registration
 * failed; no unload or unregister exists after a successful PEP registration. */
_IRQL_requires_(PASSIVE_LEVEL)
VOID DegasBackendShutdown(VOID);
#endif
