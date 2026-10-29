#ifndef DEGAS_IOCTL_H
#define DEGAS_IOCTL_H
#ifdef _KERNEL_MODE
#include <ntddk.h>
#else
#include <windows.h>
#include <winioctl.h>
#endif

#define IOCTL_DEGAS_DVFS_SNAPSHOT CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_DATA)
#define DEGAS_DVFS_SNAPSHOT_VERSION 4
#define DEGAS_DVFS_WIN32_PATH L"\\\\.\\DegasDvfs"

typedef struct _DEGAS_DVFS_DOMAIN_STATE {
    ULONG DomainId;
    ULONG LowestKhz;
    ULONG LowestNonlinearKhz;
    ULONG NominalKhz;
    ULONG HighestKhz;
    ULONG TransitionLatency100ns;
    ULONG TransitionOverhead100ns;
    ULONG RequestKhz;
    ULONG CeilingKhz;
    ULONG LimitReasons;
    ULONG Ready;
    LONG TemperatureMilliC;
    ULONG ThermalStatus;
    ULONG ThermalValid;
    ULONG SnapshotValid;
    ULONG FaultLatched;
} DEGAS_DVFS_DOMAIN_STATE;

/* Pack only the appended probe ABI: retain the complete 556-byte v3 prefix. */
#pragma pack(push, 4)
typedef struct _DEGAS_AMU_CPU_STATE {
    ULONG Status;
    ULONG Uid;
    ULONGLONG Mpidr;
    ULONGLONG Pfr0;
    ULONGLONG Enable;
    ULONGLONG ReferenceHz;
    ULONG MeasuredKhz;
    ULONG Attempts;
    ULONGLONG CoreBefore;
    ULONGLONG ConstantBefore;
    ULONGLONG TimerBefore;
    ULONGLONG CoreAfter;
    ULONGLONG ConstantAfter;
    ULONGLONG TimerAfter;
} DEGAS_AMU_CPU_STATE;
typedef struct _DEGAS_AMU_STATE {
    LONG InitStatus;
    ULONG QualifiedMask;
    ULONG ProcessorCount;
    ULONG ProbeComplete;
    DEGAS_AMU_CPU_STATE Cpus[8];
} DEGAS_AMU_STATE;
#pragma pack(pop)

typedef struct _DEGAS_DVFS_SNAPSHOT {
    ULONG Version;
    ULONG Size;
    LONG BackendInitStatus;
    LONG PepRegistrationStatus;
    ULONG PepRegistered;
    ULONG DomainCount;
    ULONG RegisteredProcessorMask;
    ULONG Reserved;
    DEGAS_DVFS_DOMAIN_STATE Domains[3];
    ULONG RawThermalValid;
    LONG RawCoreTemperatureMilliC[8];
    ULONG RawCeilingKhz[3];
    ULONG RawUtcCount;
    ULONG FreshnessQualified;
    ULONG DpmPrepareCount;
    ULONG DpmRegisterCount;
    ULONG PpmNotificationCount;
    ULONG PerfSetCount;
    WCHAR LastRejectedDeviceId[128];
    ULONG LastAcceptedUid; /* MAXULONG means no processor accepted. */
    DEGAS_AMU_STATE Amu;
} DEGAS_DVFS_SNAPSHOT;
#endif
