/* Historical observer-v2 regression source. The activation backend deliberately
 * permits early initialization with a closed guard; use test_backend_dynamic.c. */
#include <assert.h>
#include <stdio.h>
#include "../degas_backend.h"
#include "../degas_thermal.h"

static ULONG memory[0x1400/4];
static unsigned int reads, writes, waits, maps;

void *MmMapIoSpaceEx(PHYSICAL_ADDRESS Address, size_t Length, ULONG Protection)
{
    assert(Address.QuadPart == 0x11bc00 && Length == sizeof(memory));
    assert(Protection == (PAGE_READWRITE | PAGE_NOCACHE));
    ++maps;
    return memory;
}
void MmUnmapIoSpace(void *Address, size_t Length)
{
    assert(Address == memory && Length == sizeof(memory));
}
ULONG READ_REGISTER_ULONG(PULONG Address)
{
    assert(Address >= memory && Address < memory + sizeof(memory)/sizeof(memory[0]));
    ++reads;
    return *Address;
}
void WRITE_REGISTER_ULONG(PULONG Address, ULONG Value)
{
    (void)Address; (void)Value;
    ++writes;
}
void KeMemoryBarrier(void) {}
int KeDelayExecutionThread(int Mode, int Alertable, LARGE_INTEGER *Delay)
{
    (void)Mode; (void)Alertable; (void)Delay;
    ++waits;
    return 0;
}
int DbgPrintEx(int Component, int Level, const char *Format, ...)
{
    (void)Component; (void)Level; (void)Format;
    return 0;
}
NTSTATUS DegasThermalInitialize(void) { return STATUS_SUCCESS; }
void DegasThermalShutdown(void) {}
BOOLEAN DegasThermalRead(ULONG Domain, ULONG *Ceiling, LONG *Temperature)
{
    assert(Domain < 3);
    *Ceiling = 0; *Temperature = 0;
    return FALSE;
}
BOOLEAN DegasThermalReadTelemetry(DEGAS_THERMAL_TELEMETRY *Snapshot)
{
    ULONG i;
    memset(Snapshot, 0, sizeof(*Snapshot));
    for (i = 0; i < 8; ++i) Snapshot->CoreTemperatureMilliC[i] = 35000 + (LONG)i*100;
    Snapshot->FreshnessQualified = FALSE;
    return TRUE;
}

int main(void)
{
    const ULONG highest[3] = {2200000,3200000,3350000};
    const ULONG boot[3] = {1300000,1300000,1200000};
    DEGAS_BACKEND_INFO info;
    DEGAS_DOMAIN_SNAPSHOT snap;
    ULONG d, at, lowest;
    NTSTATUS status;
    memory[0x4cc/4] = 1;
    for (d = 0; d < 3; ++d) {
        at = (0x10 + d*0x120)/4;
        lowest = d == 0 ? 480 : 400;
        memory[at] = highest[d]/1000;
        memory[at+1] = boot[d]/1000;
        memory[at+2] = lowest;
        memory[at+3] = lowest; /* Vendor duplicate terminator. */
        memory[at+0x84/4] = 1;
        memory[at+0x88/4] = boot[d];
        memory[at+0x8c/4] = 3;
    }
    status = DegasBackendInitialize(&info);
    assert(status == STATUS_DEVICE_NOT_READY);
    assert(maps == 1 && reads > 0 && writes == 0 && waits == 0);
    for (d = 0; d < 3; ++d) {
        assert(info.Domains[d].HighestKhz == highest[d]);
        assert(DegasBackendReadSnapshot(d, &snap));
        assert(snap.RequestKhz == boot[d] && snap.Ready);
        assert(!snap.ThermalValid && snap.ThermalStatus == (ULONG)STATUS_DEVICE_NOT_READY);
        assert(snap.CeilingKhz == info.Domains[d].LowestKhz);
        assert(!DegasBackendSetDomainKhz(d, highest[d], info.Domains[d].LowestKhz, highest[d]));
    }
    assert(writes == 0 && waits == 0);
    DegasBackendShutdown();
    assert(!DegasBackendReadSnapshot(0, &snap));
    puts("PASS: actual backend thermal-closed init keeps all3 caps/read-only snapshots; zero writes/waits; rejected stock-max sets");
    return 0;
}
