/* Performance-only boot-start WDM PEP. Hardware lives in degas_backend.c. */
#define PEP_KERNEL_INFORMATION_VERSION 3
#include <ntddk.h>
#include <pep_x.h>
#include <wdmsec.h>
#include "degas_backend.h"
#include "degas_protocol.h"
#include "degas_ioctl.h"
#include "degas_thermal.h"
#include "degas_ramlog.h"
#include "degas_amu.h"

typedef struct _DEGAS_CPU {
    ULONG Uid;
    ULONG Domain;
    volatile LONG Registered;
    volatile LONG Started;
    volatile LONG ConstraintPending;
    volatile LONG ConstraintInFlight;
    PEP_WORK_INFORMATION ConstraintWork;
    POHANDLE KernelHandle;
} DEGAS_CPU;

static DEGAS_CPU gCpus[8];
static DEGAS_BACKEND_INFO gInfo;
static PEP_KERNEL_INFORMATION gKernel;
static KSPIN_LOCK gDomainLocks[DEGAS_DOMAIN_COUNT];
static volatile LONG gDomainFault[DEGAS_DOMAIN_COUNT];
static const ULONG gMembers[DEGAS_DOMAIN_COUNT] = {4, 3, 1};
static const ULONG gStockMax[DEGAS_DOMAIN_COUNT] = {2200000, 3200000, 3350000};
static NTSTATUS gBackendStatus = STATUS_DEVICE_NOT_READY;
static NTSTATUS gRamStatus = STATUS_DEVICE_NOT_READY;
static NTSTATUS gRegistrationStatus = STATUS_DEVICE_NOT_READY;
static volatile LONG gPepRegistered;
static volatile LONG gInitializationComplete;
static volatile LONG gDpmPrepareCount, gDpmRegisterCount, gPpmNotificationCount, gPerfSetCount;
static ULONG gLastAcceptedUid = MAXULONG;
static WCHAR gLastRejectedDeviceId[128];
static KSPIN_LOCK gDiagnosticLock;
static const GUID gConstraintGuid =
    {0x29181fa1, 0x4bf3, 0x4c2e, {0xb3,0x14,0xa6,0xd2,0x26,0x32,0x2b,0x00}};
/* Private class GUID only scopes this diagnostic device's explicit ACL. */
static const GUID gDeviceClass =
    {0x6eccbe61, 0xa64c, 0x4f75, {0x9d,0xe8,0xf6,0xb1,0x3a,0xd5,0x00,0x97}};

C_ASSERT(sizeof(DEGAS_DVFS_SNAPSHOT) == 1340);
C_ASSERT(FIELD_OFFSET(DEGAS_DVFS_SNAPSHOT, Amu) == 556);
C_ASSERT(sizeof(DEGAS_AMU_CPU_STATE) == 96);
C_ASSERT(FIELD_OFFSET(DEGAS_DVFS_SNAPSHOT, RawThermalValid) == 224);
C_ASSERT(FIELD_OFFSET(DEGAS_DVFS_SNAPSHOT, DpmPrepareCount) == 280);

static BOOLEAN DegasPathContains(PCUNICODE_STRING Path, PCWSTR Needle)
{
    USHORT i, j, n;
    if (!Path || !Path->Buffer) return FALSE;
    n = Path->Length / sizeof(WCHAR);
    if (n > 127) n = 127;
    for (i = 0; i < n; ++i) {
        for (j = 0; Needle[j] && i + j < n; ++j) {
            WCHAR c = Path->Buffer[i+j];
            if (c >= L'a' && c <= L'z') c -= L'a' - L'A';
            if (c != Needle[j]) break;
        }
        if (!Needle[j]) return TRUE;
    }
    return FALSE;
}

static VOID DegasRememberRejected(PCUNICODE_STRING Id)
{
    KIRQL oldIrql;
    ULONG count;
    BOOLEAN relevant;
    if (!Id || !Id->Buffer || (Id->Length % sizeof(WCHAR))) return;
    relevant = DegasPathContains(Id, L"C00") || DegasPathContains(Id, L"CPU") ||
        DegasPathContains(Id, L"ACPI0007") || DegasPathContains(Id, L"ACPI0010");
    count = Id->Length / sizeof(WCHAR);
    if (count >= RTL_NUMBER_OF(gLastRejectedDeviceId)) count = RTL_NUMBER_OF(gLastRejectedDeviceId)-1;
    KeAcquireSpinLock(&gDiagnosticLock, &oldIrql);
    if (relevant || (gLastAcceptedUid == MAXULONG && !gLastRejectedDeviceId[0])) {
        RtlZeroMemory(gLastRejectedDeviceId, sizeof(gLastRejectedDeviceId));
        RtlCopyMemory(gLastRejectedDeviceId, Id->Buffer, count*sizeof(WCHAR));
    }
    KeReleaseSpinLock(&gDiagnosticLock, oldIrql);
}

static VOID DegasSnapshot(DEGAS_DVFS_SNAPSHOT *Output)
{
    ULONG i;
    KIRQL oldIrql;
    DEGAS_THERMAL_TELEMETRY raw;
    RtlZeroMemory(Output, sizeof(*Output));
    Output->Version = DEGAS_DVFS_SNAPSHOT_VERSION;
    Output->Size = sizeof(*Output);
    Output->BackendInitStatus = gBackendStatus;
    Output->PepRegistrationStatus = gRegistrationStatus;
    Output->PepRegistered = (ULONG)InterlockedCompareExchange(&gPepRegistered, 0, 0);
    Output->DomainCount = DEGAS_DOMAIN_COUNT;
    Output->Reserved = (ULONG)gRamStatus;
    Output->DpmPrepareCount = (ULONG)InterlockedCompareExchange(&gDpmPrepareCount, 0, 0);
    Output->DpmRegisterCount = (ULONG)InterlockedCompareExchange(&gDpmRegisterCount, 0, 0);
    Output->PpmNotificationCount = (ULONG)InterlockedCompareExchange(&gPpmNotificationCount, 0, 0);
    Output->PerfSetCount = (ULONG)InterlockedCompareExchange(&gPerfSetCount, 0, 0);
    KeAcquireSpinLock(&gDiagnosticLock, &oldIrql);
    Output->LastAcceptedUid = gLastAcceptedUid;
    RtlCopyMemory(Output->LastRejectedDeviceId, gLastRejectedDeviceId, sizeof(gLastRejectedDeviceId));
    KeReleaseSpinLock(&gDiagnosticLock, oldIrql);
    /* The control name exists before backend initialization; do not race its
     * partially published mappings or capability fields. */
    if (!InterlockedCompareExchange(&gInitializationComplete, 0, 0)) return;
    DegasAmuReadSnapshot(&Output->Amu);
    RtlZeroMemory(&raw, sizeof(raw));
    if (DegasThermalReadTelemetry(&raw)) {
        Output->RawThermalValid = TRUE;
        for (i = 0; i < RTL_NUMBER_OF(raw.CoreTemperatureMilliC); ++i)
            Output->RawCoreTemperatureMilliC[i] = raw.CoreTemperatureMilliC[i];
        for (i = 0; i < RTL_NUMBER_OF(raw.CeilingKhz); ++i)
            Output->RawCeilingKhz[i] = raw.CeilingKhz[i];
        Output->RawUtcCount = raw.UtcCount;
        Output->FreshnessQualified = raw.FreshnessQualified;
    }
    for (i = 0; i < RTL_NUMBER_OF(gCpus); ++i)
        if (InterlockedCompareExchange(&gCpus[i].Registered, 2, 2) == 2)
            Output->RegisteredProcessorMask |= 1UL << i;
    for (i = 0; i < DEGAS_DOMAIN_COUNT; ++i) {
        DEGAS_DVFS_DOMAIN_STATE *p = &Output->Domains[i];
        DEGAS_DOMAIN_LIMITS *limits = &gInfo.Domains[i];
        DEGAS_DOMAIN_SNAPSHOT snap;
        p->DomainId = i;
        p->LowestKhz = limits->LowestKhz;
        p->LowestNonlinearKhz = limits->LowestNonlinearKhz;
        p->NominalKhz = limits->NominalKhz;
        p->HighestKhz = limits->HighestKhz;
        p->TransitionLatency100ns = limits->TransitionLatency100ns;
        p->TransitionOverhead100ns = limits->TransitionOverhead100ns;
        p->FaultLatched = (ULONG)InterlockedCompareExchange(&gDomainFault[i], 0, 0);
        RtlZeroMemory(&snap, sizeof(snap));
        if (DegasBackendReadSnapshot(i, &snap)) {
            p->SnapshotValid = TRUE;
            p->RequestKhz = snap.RequestKhz;
            p->CeilingKhz = snap.CeilingKhz;
            p->LimitReasons = snap.LimitReasons;
            p->Ready = snap.Ready;
            p->TemperatureMilliC = snap.TemperatureMilliC;
            p->ThermalStatus = snap.ThermalStatus;
            p->ThermalValid = snap.ThermalValid;
        }
    }
}

DRIVER_DISPATCH DegasControl;
NTSTATUS DegasControl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG_PTR bytes = 0;
    UNREFERENCED_PARAMETER(DeviceObject);
    if (stack->MajorFunction == IRP_MJ_CREATE ||
        stack->MajorFunction == IRP_MJ_CLOSE ||
        stack->MajorFunction == IRP_MJ_CLEANUP) {
        status = STATUS_SUCCESS;
    } else if (stack->MajorFunction == IRP_MJ_DEVICE_CONTROL &&
        stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_DEGAS_DVFS_SNAPSHOT) {
        if (stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(DEGAS_DVFS_SNAPSHOT)) {
            status = STATUS_BUFFER_TOO_SMALL;
        } else if (!Irp->AssociatedIrp.SystemBuffer) {
            status = STATUS_INVALID_PARAMETER;
        } else {
            DegasSnapshot(Irp->AssociatedIrp.SystemBuffer);
            bytes = sizeof(DEGAS_DVFS_SNAPSHOT);
            status = STATUS_SUCCESS;
        }
    }
    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = bytes;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

static NTSTATUS DegasCreateControl(PDRIVER_OBJECT DriverObject)
{
    UNICODE_STRING deviceName, linkName, acl;
    PDEVICE_OBJECT device;
    NTSTATUS status;
    ULONG i;
    RtlInitUnicodeString(&deviceName, L"\\Device\\DegasDvfs");
    RtlInitUnicodeString(&linkName, L"\\DosDevices\\DegasDvfs");
    RtlInitUnicodeString(&acl, L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");
    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; ++i)
        DriverObject->MajorFunction[i] = DegasControl;
    status = IoCreateDeviceSecure(DriverObject, 0, &deviceName, FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN, FALSE, &acl, &gDeviceClass, &device);
    if (!NT_SUCCESS(status)) return status;
    status = IoCreateSymbolicLink(&linkName, &deviceName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(device);
        return status;
    }
    device->Flags |= DO_BUFFERED_IO;
    device->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static int DegasPathUid(PCUNICODE_STRING Id)
{
    if (!Id || (Id->Length % sizeof(WCHAR)) != 0) return -1;
    return DegasDecodeProcessorPath(Id->Buffer, Id->Length / sizeof(WCHAR));
}

static DEGAS_CPU *DegasCpuFromHandle(PEPHANDLE Handle)
{
    ULONG i;
    for (i = 0; i < RTL_NUMBER_OF(gCpus); ++i) {
        if (Handle == (PEPHANDLE)&gCpus[i] &&
            InterlockedCompareExchange(&gCpus[i].Registered, 2, 2) == 2)
            return &gCpus[i];
    }
    return NULL;
}

static VOID DegasRequestConstraintWorker(VOID)
{
    if (InterlockedCompareExchange(&gPepRegistered, 0, 0) && gKernel.RequestWorker)
        gKernel.RequestWorker(gKernel.Plugin);
}

VOID DegasPepNotifyConstraints(ULONG DomainId)
{
    ULONG i;
    BOOLEAN queued = FALSE;
    if (DomainId >= DEGAS_DOMAIN_COUNT) return;
    for (i = 0; i < RTL_NUMBER_OF(gCpus); ++i) {
        DEGAS_CPU *cpu = &gCpus[i];
        if (cpu->Domain == DomainId &&
            InterlockedCompareExchange(&cpu->Registered, 2, 2) == 2 &&
            InterlockedCompareExchange(&cpu->Started, 1, 1) == 1) {
            InterlockedExchange(&cpu->ConstraintPending, 1);
            queued = TRUE;
        }
    }
    if (queued) DegasRequestConstraintWorker();
}

static BOOLEAN DegasConstraintWork(PPEP_WORK Work)
{
    ULONG i;
    Work->NeedWork = FALSE;
    /* This installed NT dispatches the buffer it supplied, rather than
     * reloading a substituted pointer. Preserve the borrowed pointer. */
    if (!Work->WorkInformation) return TRUE;
    for (i = 0; i < RTL_NUMBER_OF(gCpus); ++i) {
        DEGAS_CPU *cpu = &gCpus[i];
        PEP_WORK_INFORMATION *info = Work->WorkInformation;
        if (InterlockedCompareExchange(&cpu->Registered, 2, 2) != 2 ||
            InterlockedCompareExchange(&cpu->Started, 1, 1) != 1 ||
            InterlockedCompareExchange(&cpu->ConstraintPending, 0, 0) != 1 ||
            InterlockedCompareExchange(&cpu->ConstraintInFlight, 1, 0) != 0)
            continue;
        InterlockedExchange(&cpu->ConstraintPending, 0);
        RtlZeroMemory(info, sizeof(*info));
        info->WorkType = PepWorkRequestPowerControl;
        info->PowerControl.DeviceHandle = cpu->KernelHandle;
        info->PowerControl.PowerControlCode = &gConstraintGuid;
        info->PowerControl.RequestContext = cpu;
        /* No input/output buffers for this control. The borrowed work buffer
         * is consumed by NT; only the resident CPU request context is retained
         * until POWER_CONTROL_COMPLETE. Publish NeedWork after filling it. */
        Work->NeedWork = TRUE;
        DegasRequestConstraintWorker(); /* Drain other eligible CPU notifications. */
        break;
    }
    return TRUE;
}

PEPCALLBACKNOTIFYDPM DegasAcceptDevice;
PEPCALLBACKNOTIFYPPM DegasAcceptProcessor;
DRIVER_INITIALIZE DriverEntry;

BOOLEAN DegasAcceptDevice(ULONG Notification, PVOID Data)
{
    int uid;
    DegasRamMark(0x10, Notification, (ULONG_PTR)Data);
    if (!Data) return FALSE;
    switch (Notification) {
    case PEP_DPM_PREPARE_DEVICE: {
        PPEP_PREPARE_DEVICE p = Data;
        InterlockedIncrement(&gDpmPrepareCount);
        uid = DegasPathUid(p->DeviceId);
        p->DeviceAccepted = uid >= 0;
        if (uid < 0) DegasRememberRejected(p->DeviceId);
        if (uid < 0 && p->DeviceId)
            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                "DegasDvfs: declined DeviceId=%wZ\n", p->DeviceId);
        /* TRUE acknowledges the notification; DeviceAccepted is ownership.
         * NT bugchecks A0/605 if this mandatory callback returns FALSE. */
        return TRUE;
    }
    case PEP_DPM_ABANDON_DEVICE: {
        PPEP_ABANDON_DEVICE p = Data;
        uid = DegasPathUid(p->DeviceId);
        p->DeviceAccepted = uid >= 0;
        return TRUE;
    }
    case PEP_DPM_REGISTER_DEVICE: {
        PPEP_REGISTER_DEVICE p = Data;
        KIRQL oldIrql;
        InterlockedIncrement(&gDpmRegisterCount);
        p->DeviceAccepted = PepDeviceNotAccepted;
        p->DeviceHandle = NULL;
        uid = DegasPathUid(p->DeviceId);
        if (uid < 0) DegasRememberRejected(p->DeviceId);
        if (uid < 0 || !p->KernelHandle ||
            InterlockedCompareExchange(&gCpus[uid].Registered, 1, 0) != 0)
            return TRUE; /* Handled decline, including duplicate registration. */
        gCpus[uid].KernelHandle = p->KernelHandle;
        InterlockedExchange(&gCpus[uid].Registered, 2);
        p->DeviceHandle = (PEPHANDLE)&gCpus[uid];
        p->DeviceAccepted = PepDeviceAccepted;
        KeAcquireSpinLock(&gDiagnosticLock, &oldIrql);
        gLastAcceptedUid = (ULONG)uid;
        KeReleaseSpinLock(&gDiagnosticLock, oldIrql);
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
            "DegasDvfs: processor UID=%u domain=%u registered\n",
            (ULONG)uid, gCpus[uid].Domain);
        return TRUE;
    }
    case PEP_DPM_UNREGISTER_DEVICE: {
        PPEP_UNREGISTER_DEVICE p = Data;
        DEGAS_CPU *cpu = DegasCpuFromHandle(p->DeviceHandle);
        if (!cpu) return TRUE;
        InterlockedExchange(&gDomainFault[cpu->Domain], 1);
        InterlockedExchange(&cpu->Started, 0);
        InterlockedExchange(&cpu->Registered, 0);
        return TRUE;
    }
    case PEP_DPM_DEVICE_STARTED: {
        DEGAS_CPU *cpu = DegasCpuFromHandle(((PPEP_DEVICE_STARTED)Data)->DeviceHandle);
        if (!cpu) return TRUE;
        InterlockedExchange(&cpu->Started, 1);
        DegasPepNotifyConstraints(cpu->Domain);
        return TRUE;
    }
    case PEP_DPM_WORK:
        return DegasConstraintWork(Data);
    case PEP_DPM_DEVICE_POWER_STATE:
        /* Every field is input. Performance ownership adds no external power
         * resource or Dx transition; native PSCI remains its existing owner. */
        return TRUE;
    case PEP_DPM_SYSTEM_LATENCY_UPDATE:
        return TRUE; /* Read-only global advisory; no idle policy is claimed. */
    case PEP_DPM_COMPONENT_ACTIVE: {
        PPEP_COMPONENT_ACTIVE p = Data;
        p->WorkInformation = NULL;
        p->NeedWork = FALSE; /* No external component resource transition. */
        return TRUE;
    }
    case PEP_DPM_NOTIFY_COMPONENT_IDLE_STATE:
        /* No external resources to prepare. Only Completed is output; this
         * acknowledges device Fx preparation and does not execute CPU idle. */
        ((PPEP_NOTIFY_COMPONENT_IDLE_STATE)Data)->Completed = TRUE;
        return TRUE;
    case PEP_DPM_POWER_CONTROL_REQUEST: {
        PPEP_POWER_CONTROL_REQUEST p = Data;
        p->BytesReturned = 0;
        p->Status = STATUS_NOT_SUPPORTED;
        return TRUE; /* Acknowledge with an explicit unsupported operation. */
    }
    case PEP_DPM_POWER_CONTROL_COMPLETE: {
        PPEP_POWER_CONTROL_COMPLETE p = Data;
        DEGAS_CPU *cpu = DegasCpuFromHandle((PEPHANDLE)p->RequestContext);
        if (!cpu || !p->PowerControlCode ||
            RtlCompareMemory(p->PowerControlCode, &gConstraintGuid, sizeof(GUID)) != sizeof(GUID))
            return TRUE; /* Completion acknowledged; never reuse unknown context. */
        InterlockedExchange(&cpu->ConstraintInFlight, 0);
        if (InterlockedCompareExchange(&cpu->ConstraintPending, 0, 0))
            DegasRequestConstraintWorker();
        return TRUE;
    }
    default:
        return FALSE;
    }
}

static BOOLEAN DegasSetPerformance(DEGAS_CPU *Cpu, PPEP_PPM_PERF_SET Set)
{
    ULONG domain = Cpu->Domain;
    DEGAS_DOMAIN_LIMITS *limits = &gInfo.Domains[domain];
    ULONG minimum, maximum;
    unsigned int selected;
    BOOLEAN ok = FALSE;
    /* PERF_SET is documented DISPATCH_LEVEL. Fail closed if called otherwise. */
    if (KeGetCurrentIrql() != DISPATCH_LEVEL) return FALSE;
    /* Validate the OS/static hardware interval, not its transient thermal
     * intersection. Backend must still lower safely when thermal < OS minimum. */
    if (!DegasSelectRequest(limits->LowestKhz, limits->HighestKhz, limits->HighestKhz,
        Set->MinimumPerformance, Set->MaximumPerformance, Set->DesiredPerformance, &selected))
        return FALSE;
    minimum = max(limits->LowestKhz, Set->MinimumPerformance);
    maximum = min(limits->HighestKhz, Set->MaximumPerformance);
    KeAcquireSpinLockAtDpcLevel(&gDomainLocks[domain]);
    if (InterlockedCompareExchange(&gDomainFault[domain], 0, 0) == 0)
        ok = DegasBackendSetDomainKhz(domain, selected, minimum, maximum);
    /* There is no output NTSTATUS in PERF_SET. Reject rather than fabricate a
     * completed transition; latch hardware failures and stop further writes. */
    if (!ok) InterlockedExchange(&gDomainFault[domain], 1);
    KeReleaseSpinLockFromDpcLevel(&gDomainLocks[domain]);
    /* Telemetry exposes the fault. No debugger output or worker scheduling in
     * the bounded runtime transition callback. */
    return ok;
}

BOOLEAN DegasAcceptProcessor(PEPHANDLE Handle, ULONG Notification, PVOID Data)
{
    DEGAS_CPU *cpu;
    DEGAS_DOMAIN_LIMITS *limits;
    if (Notification != PEP_NOTIFY_PPM_PERF_SET)
        DegasRamMark(0x20, Notification, (ULONG_PTR)Handle);
    InterlockedIncrement(&gPpmNotificationCount);
    if (Notification == PEP_NOTIFY_PPM_PERF_SET) InterlockedIncrement(&gPerfSetCount);
    if (!Data) return FALSE;
    /* Domain query is global, with its own input DomainId; Handle may be NULL. */
    if (Notification == PEP_NOTIFY_PPM_QUERY_DOMAIN_INFO) {
        PPEP_PPM_QUERY_DOMAIN_INFO p = Data;
        if (p->DomainId >= DEGAS_DOMAIN_COUNT) return FALSE;
        limits = &gInfo.Domains[p->DomainId];
        p->CoordinationType = PROCESSOR_DOMAIN_COORDINATION_SW_ANY;
        p->IdleProcessorsDiscounted = FALSE;
        p->SchedulerDirectedTransitionsSupported = FALSE;
        p->AffinitizePerfSet = FALSE;
        p->WorstCaseTransitionLatency = limits->TransitionLatency100ns;
        p->WorstCaseTransitionOverhead = limits->TransitionOverhead100ns;
        return TRUE;
    }
    cpu = DegasCpuFromHandle(Handle);
    if (!cpu) return FALSE;
    limits = &gInfo.Domains[cpu->Domain];
    switch (Notification) {
    case PEP_NOTIFY_PPM_QUERY_CAPABILITIES: {
        PPEP_PPM_QUERY_CAPABILITIES p = Data;
        RtlZeroMemory(p, sizeof(*p));
        p->PerformanceStatesSupported = TRUE;
        return TRUE;
    }
    case PEP_NOTIFY_PPM_QUERY_PERF_CAPABILITIES: {
        PPEP_PPM_QUERY_PERF_CAPABILITIES p = Data;
        p->HighestPerformance = limits->HighestKhz;
        p->NominalPerformance = limits->NominalKhz;
        p->LowestNonlinearPerformance = limits->LowestNonlinearKhz;
        p->LowestPerformance = limits->LowestKhz;
        p->DomainId = cpu->Domain;
        p->DomainMembers = gMembers[cpu->Domain];
        return TRUE;
    }
    case PEP_NOTIFY_PPM_PERF_CONSTRAINTS: {
        PPEP_PPM_PERF_CONSTRAINTS p = Data;
        DEGAS_DOMAIN_SNAPSHOT snap;
        if (!DegasBackendReadSnapshot(cpu->Domain, &snap) || !snap.Ready)
            return FALSE;
        p->GuaranteedPerformanceLimit = snap.CeilingKhz < limits->HighestKhz ?
            snap.CeilingKhz : limits->HighestKhz;
        p->LimitReasons = snap.LimitReasons;
        return TRUE;
    }
    case PEP_NOTIFY_PPM_PERF_SET:
        return DegasSetPerformance(cpu, Data);
    case PEP_NOTIFY_PPM_QUERY_FEEDBACK_COUNTERS:
        ((PPEP_PPM_QUERY_FEEDBACK_COUNTERS)Data)->Count = 0;
        return TRUE;
    case PEP_NOTIFY_PPM_QUERY_PERF_EPP_SUPPORT:
        ((PPEP_PPM_QUERY_PERF_EPP_SUPPORT)Data)->EnergyPerformancePreferenceSupported = FALSE;
        return TRUE;
    case PEP_NOTIFY_PPM_QUERY_AUTONOMOUS_SUPPORT:
        ((PPEP_PPM_QUERY_PERF_AUTONOMOUS_SUPPORT)Data)->AutonomousCapability =
            PROCESSOR_AUTONOMOUS_CAPABILITY_UNSUPPORTED;
        return TRUE;
    default:
        /* No idle, suspend/resume, platform, critical-resource or PSCI callback. */
        return FALSE;
    }
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    PEP_INFORMATION pep;
    ULONG i;
    NTSTATUS status;
    UNREFERENCED_PARAMETER(RegistryPath);
    DriverObject->DriverUnload = NULL; /* A registered PEP cannot unregister. */
    for (i = 0; i < RTL_NUMBER_OF(gCpus); ++i) {
        gCpus[i].Uid = i;
        gCpus[i].Domain = DegasDomainFromUid(i);
    }
    for (i = 0; i < DEGAS_DOMAIN_COUNT; ++i)
        KeInitializeSpinLock(&gDomainLocks[i]);
    KeInitializeSpinLock(&gDiagnosticLock);
    gRamStatus = DegasRamInitialize(); /* Logging failure never blocks PEP. */
    status = DegasCreateControl(DriverObject);
    if (!NT_SUCCESS(status)) return status;
    (VOID)DegasAmuInitialize(); /* Read-only probe failure never blocks PEP. */
    DegasRamMark(2, 0, 0);
    gBackendStatus = DegasBackendInitialize(&gInfo);
    DegasRamMark(3, 0, (ULONG_PTR)(ULONG)gBackendStatus);
    if (!NT_SUCCESS(gBackendStatus)) {
        InterlockedExchange(&gInitializationComplete, 1);
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL,
            "DegasDvfs: observe-only backend status=%08x; PEP not registered\n", gBackendStatus);
        return STATUS_SUCCESS;
    }
    for (i = 0; i < DEGAS_DOMAIN_COUNT; ++i) {
        DEGAS_DOMAIN_LIMITS *p = &gInfo.Domains[i];
        if (!p->LowestKhz || p->LowestKhz > p->LowestNonlinearKhz ||
            p->LowestNonlinearKhz > p->NominalKhz ||
            p->NominalKhz > p->HighestKhz || p->HighestKhz > gStockMax[i] ||
            !p->TransitionLatency100ns) {
            gBackendStatus = STATUS_DEVICE_CONFIGURATION_ERROR;
            InterlockedExchange(&gInitializationComplete, 1);
            return STATUS_SUCCESS; /* Keep read-only diagnostics available. */
        }
    }
    InterlockedExchange(&gInitializationComplete, 1);
    RtlZeroMemory(&pep, sizeof(pep));
    pep.Version = PEP_INFORMATION_VERSION;
    pep.Size = sizeof(pep);
    pep.AcceptDeviceNotification = DegasAcceptDevice;
    pep.AcceptProcessorNotification = DegasAcceptProcessor;
    /* NULL ACPI callback: existing AML remains the ACPI services provider. */
    RtlZeroMemory(&gKernel, sizeof(gKernel));
    gKernel.Version = PEP_KERNEL_INFORMATION_VERSION;
    gKernel.Size = sizeof(gKernel);
    DegasRamMark(4, 0, 0);
    gRegistrationStatus = PoFxRegisterPlugin(&pep, &gKernel);
    DegasRamMark(5, 0, (ULONG_PTR)(ULONG)gRegistrationStatus);
    if (NT_SUCCESS(gRegistrationStatus)) {
        InterlockedExchange(&gPepRegistered, 1);
        for (i = 0; i < DEGAS_DOMAIN_COUNT; ++i) DegasPepNotifyConstraints(i);
    }
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "DegasDvfs: PEP registration status=%08x\n", gRegistrationStatus);
    return STATUS_SUCCESS; /* Preserve control telemetry even on rejected PEP. */
}
