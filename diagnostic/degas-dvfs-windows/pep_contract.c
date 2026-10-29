/* Offline WDK ABI scaffold. No DriverEntry, registration, MMIO, or installation. */
#include <ntddk.h>
#include <pep_x.h>

typedef struct _DEGAS_DOMAIN_CONTRACT {
    ULONG Id;
    ULONG CpuMask;
    ULONG Members;
    ULONG CsramBase;
} DEGAS_DOMAIN_CONTRACT;

const DEGAS_DOMAIN_CONTRACT DegasDomains[3] = {
    {0, 0x0f, 4, 0x0011bc10},
    {1, 0x70, 3, 0x0011bd30},
    {2, 0x80, 1, 0x0011be50}
};

C_ASSERT(sizeof(PEP_PPM_PERF_SET) == 20);
C_ASSERT(FIELD_OFFSET(PEP_PPM_QUERY_PERF_CAPABILITIES, DomainId) == 16);
C_ASSERT(FIELD_OFFSET(PEP_PPM_QUERY_PERF_CAPABILITIES, DomainMembers) == 20);
C_ASSERT(FIELD_OFFSET(PEP_PPM_QUERY_DOMAIN_INFO, WorstCaseTransitionLatency) == 8);

PEPCALLBACKNOTIFYDPM DegasRejectDevice;
PEPCALLBACKNOTIFYPPM DegasRejectProcessor;

BOOLEAN DegasRejectDevice(ULONG Notification, PVOID Data)
{
    UNREFERENCED_PARAMETER(Notification);
    UNREFERENCED_PARAMETER(Data);
    return FALSE;
}

BOOLEAN DegasRejectProcessor(PEPHANDLE Handle, ULONG Notification, PVOID Data)
{
    UNREFERENCED_PARAMETER(Handle);
    UNREFERENCED_PARAMETER(Notification);
    UNREFERENCED_PARAMETER(Data);
    return FALSE;
}

/* For ABI review only. Caller must NOT register this rejecting placeholder. */
VOID DegasBuildOfflinePepInformation(PPEP_INFORMATION Information)
{
    RtlZeroMemory(Information, sizeof(*Information));
    Information->Version = PEP_INFORMATION_VERSION;
    Information->Size = sizeof(*Information);
    Information->AcceptDeviceNotification = DegasRejectDevice;
    Information->AcceptProcessorNotification = DegasRejectProcessor;
}
