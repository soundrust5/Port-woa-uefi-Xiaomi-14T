#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include "../degas_backend.h"
#include "../degas_thermal.h"

static ULONG memory[0x1400/4];
static unsigned int reads, enables, requests, waits, maps, notifications, closed;
static unsigned int lockDepth;
static BOOLEAN thermalValid, invalidateOnLock, terminated;
static KIRQL irql;
static PKSTART_ROUTINE worker;
static PVOID workerContext;
static jmp_buf workerYield;
static const ULONG highest[3] = {2200000,3200000,3350000};
static const ULONG lowest[3] = {480000,400000,400000};

void *MmMapIoSpaceEx(PHYSICAL_ADDRESS Address, size_t Length, ULONG Protection)
{
    assert(Address.QuadPart == 0x11bc00 && Length == sizeof(memory));
    assert(Protection == (PAGE_READWRITE | PAGE_NOCACHE));
    ++maps; return memory;
}
void MmUnmapIoSpace(void *Address, size_t Length)
{
    assert(Address == memory && Length == sizeof(memory));
    assert(!worker || terminated);
}
ULONG READ_REGISTER_ULONG(PULONG Address)
{
    assert(Address >= memory && Address < memory + sizeof(memory)/sizeof(memory[0]));
    ++reads; return *Address;
}
void WRITE_REGISTER_ULONG(PULONG Address, ULONG Value)
{
    ULONG offset = (ULONG)((Address-memory)*4), d;
    for (d=0;d<3;++d) {
        if (offset == 0x10+d*0x120+0x84) {
            assert(irql == PASSIVE_LEVEL && Value == 1); ++enables; *Address=Value; return;
        }
        if (offset == 0x10+d*0x120+0x88) {
            assert(irql == DISPATCH_LEVEL);
            assert(Value >= lowest[d] && Value <= highest[d]);
            assert(thermalValid || Value == lowest[d]);
            ++requests; *Address=Value; return;
        }
    }
    assert(!"unexpected MMIO write");
}
void KeMemoryBarrier(void) {}
LONG InterlockedExchange(volatile LONG *p, LONG v) { LONG old=*p; *p=v; return old; }
LONG InterlockedCompareExchange(volatile LONG *p,LONG v,LONG c) { LONG old=*p; if(old==c)*p=v; return old; }
void KeInitializeSpinLock(KSPIN_LOCK *Lock) { Lock->Held=0; }
static void acquire(KSPIN_LOCK *Lock)
{
    assert(!Lock->Held);
    if (invalidateOnLock) { thermalValid=FALSE; invalidateOnLock=FALSE; }
    Lock->Held=1;
    ++lockDepth;
}
void KeAcquireSpinLock(KSPIN_LOCK *Lock,KIRQL *Old) { *Old=irql; irql=DISPATCH_LEVEL; acquire(Lock); }
void KeReleaseSpinLock(KSPIN_LOCK *Lock,KIRQL Old) { assert(Lock->Held && lockDepth); Lock->Held=0; --lockDepth; irql=Old; }
void KeAcquireSpinLockAtDpcLevel(KSPIN_LOCK *Lock) { assert(irql==DISPATCH_LEVEL); acquire(Lock); }
void KeReleaseSpinLockFromDpcLevel(KSPIN_LOCK *Lock) { assert(Lock->Held && lockDepth); Lock->Held=0; --lockDepth; }
int KeDelayExecutionThread(int Mode,int Alertable,LARGE_INTEGER *Delay)
{
    (void)Mode;(void)Alertable;(void)Delay;++waits;return 0;
}
int DbgPrintEx(int Component,int Level,const char *Format,...)
{
    (void)Component;(void)Level;(void)Format;return 0;
}
void KeInitializeEvent(KEVENT *Event,int Type,BOOLEAN State) { (void)Type;Event->Signaled=State; }
LONG KeSetEvent(KEVENT *Event,int Increment,BOOLEAN Wait) { (void)Increment;(void)Wait;Event->Signaled=TRUE;return 0; }
NTSTATUS KeWaitForSingleObject(PVOID Object,int Reason,int Mode,BOOLEAN Alertable,LARGE_INTEGER *Timeout)
{
    KEVENT *event=Object;
    (void)Reason;(void)Mode;(void)Alertable;
    assert(Timeout && Timeout->QuadPart == -2000000 && irql==PASSIVE_LEVEL);
    if(event->Signaled)return STATUS_SUCCESS;
    longjmp(workerYield,1); /* Yield one deterministic worker iteration. */
}
NTSTATUS PsCreateSystemThread(HANDLE *Thread,ULONG Access,OBJECT_ATTRIBUTES *Attributes,
    HANDLE Process,PVOID ClientId,PKSTART_ROUTINE Start,PVOID Context)
{
    assert(Access==SYNCHRONIZE && Attributes->Flags==OBJ_KERNEL_HANDLE);
    assert(!Process && !ClientId && irql==PASSIVE_LEVEL);
    worker=Start;workerContext=Context;*Thread=(HANDLE)&worker;return STATUS_SUCCESS;
}
void PsTerminateSystemThread(NTSTATUS Status) { assert(Status==STATUS_SUCCESS);terminated=TRUE;longjmp(workerYield,2); }
NTSTATUS ZwWaitForSingleObject(HANDLE Handle,BOOLEAN Alertable,LARGE_INTEGER *Timeout)
{
    assert(Handle==(HANDLE)&worker && !Alertable && !Timeout && irql==PASSIVE_LEVEL);
    if(setjmp(workerYield)==0)worker(workerContext);
    assert(terminated);return STATUS_SUCCESS;
}
NTSTATUS ZwClose(HANDLE Handle) { assert(Handle==(HANDLE)&worker);++closed;return STATUS_SUCCESS; }
static void runWorker(void) { assert(worker && !terminated);if(setjmp(workerYield)==0)worker(workerContext); }
NTSTATUS DegasThermalInitialize(void) { return STATUS_SUCCESS; }
void DegasThermalShutdown(void) {}
void DegasThermalPoll(void) { assert(irql==PASSIVE_LEVEL && lockDepth==0); }
BOOLEAN DegasThermalRead(ULONG Domain,ULONG *Ceiling,LONG *Temperature)
{
    assert(Domain<3);*Ceiling=thermalValid?highest[Domain]:0;*Temperature=thermalValid?50000:0;
    return thermalValid;
}
BOOLEAN DegasThermalReadTelemetry(DEGAS_THERMAL_TELEMETRY *Snapshot)
{
    memset(Snapshot,0,sizeof(*Snapshot));Snapshot->FreshnessQualified=thermalValid;return TRUE;
}
void DegasPepNotifyConstraints(ULONG Domain) { assert(Domain<3 && lockDepth==0);++notifications; }

int main(int argc,char **argv)
{
    const ULONG boot[3]={1300000,1300000,1200000};
    DEGAS_BACKEND_INFO info;DEGAS_DOMAIN_SNAPSHOT snap;
    ULONG d,at,before,previousNotifications;
    memory[0x4cc/4]=1;
    for(d=0;d<3;++d) {
        at=(0x10+d*0x120)/4;memory[at]=highest[d]/1000;memory[at+1]=boot[d]/1000;
        memory[at+2]=lowest[d]/1000;memory[at+3]=lowest[d]/1000;
        memory[at+0x84/4]=1;memory[at+0x88/4]=boot[d];memory[at+0x8c/4]=3;
    }
    assert(DegasBackendInitialize(&info)==STATUS_SUCCESS);
    assert(maps==1 && reads>0 && enables==3 && requests==0 && waits==0);
    for(d=0;d<3;++d) {
        assert(info.Domains[d].HighestKhz==highest[d]);
        assert(DegasBackendReadSnapshot(d,&snap));
        assert(snap.CeilingKhz==lowest[d] && !snap.ThermalValid && snap.LimitReasons==1);
    }
    /* No accepted OS request yet: an unattached PEP must preserve boot requests,
     * even while the initial guard is closed or becomes stale again. */
    runWorker();assert(requests==0);
    thermalValid=TRUE;runWorker();assert(requests==0);
    thermalValid=FALSE;runWorker();assert(requests==0);
    for(d=0;d<3;++d)assert(memory[(0x10+d*0x120+0x88)/4]==boot[d]);
    if(argc>1 && strcmp(argv[1],"no-set")==0)goto shutdown;
    /* First accepted request establishes ownership, but a closed guard still
     * overrides even a stock-max OS minimum to the safe lowest request. */
    for(d=0;d<3;++d) {
        irql=DISPATCH_LEVEL;
        assert(DegasBackendSetDomainKhz(d,highest[d],highest[d],highest[d]));
        irql=PASSIVE_LEVEL;
        assert(memory[(0x10+d*0x120+0x88)/4]==lowest[d]);
        assert(DegasBackendReadSnapshot(d,&snap) && !snap.ThermalValid && snap.LimitReasons==1);
    }
    assert(requests==3);
    before=requests;runWorker();assert(requests==before);
    previousNotifications=notifications;
    thermalValid=TRUE;runWorker();
    assert(requests==before && notifications==previousNotifications+3);
    for(d=0;d<3;++d) {
        irql=DISPATCH_LEVEL;
        assert(DegasBackendSetDomainKhz(d,1305000,1302000,1400000));
        assert(memory[(0x10+d*0x120+0x88)/4]==1305000);
        assert(DegasBackendSetDomainKhz(d,1900000,1302000,1400000));
        assert(memory[(0x10+d*0x120+0x88)/4]==1400000);
        assert(DegasBackendSetDomainKhz(d,1000000,1302000,1400000));
        assert(memory[(0x10+d*0x120+0x88)/4]==1302000);
        assert(DegasBackendSetDomainKhz(d,highest[d],lowest[d],highest[d]));
        assert(memory[(0x10+d*0x120+0x88)/4]==highest[d]);
        irql=PASSIVE_LEVEL;
    }
    if(argc>1) {
        before=requests;
        if(strcmp(argv[1],"mode-loss")==0)memory[0x4cc/4]=0;
        else memory[(0x10+0x8c)/4]=0;
        irql=DISPATCH_LEVEL;
        assert(!DegasBackendSetDomainKhz(0,2000000,480000,2200000));
        irql=PASSIVE_LEVEL;assert(requests==before);
        assert(DegasBackendReadSnapshot(0,&snap) && !snap.Ready);
    } else {
        /* The worker must also sample its cap after acquiring the domain lock. */
        invalidateOnLock=TRUE;runWorker();
        for(d=0;d<3;++d)assert(memory[(0x10+d*0x120+0x88)/4]==lowest[d]);
        thermalValid=TRUE;irql=DISPATCH_LEVEL;
        assert(DegasBackendSetDomainKhz(0,2200000,2000000,2200000));
        irql=PASSIVE_LEVEL;
        /* A lower thermal update immediately before lock acquisition must win
         * over any cap sampled by Set before acquiring that same lock. */
        invalidateOnLock=TRUE;irql=DISPATCH_LEVEL;
        assert(DegasBackendSetDomainKhz(0,2200000,2000000,2200000));
        irql=PASSIVE_LEVEL;assert(memory[(0x10+0x88)/4]==lowest[0]);
        assert(DegasBackendReadSnapshot(0,&snap) && snap.LimitReasons==1);
        runWorker();
        for(d=0;d<3;++d)assert(memory[(0x10+d*0x120+0x88)/4]==lowest[d]);
    }
shutdown:
    DegasBackendShutdown();assert(terminated && closed==1);
    assert(!DegasBackendReadSnapshot(0,&snap));
    if(argc>1 && strcmp(argv[1],"no-set")==0)
        puts("PASS (no-set): closed/fresh/stale worker preserves boot requests with zero frequency writes; joined shutdown");
    else
        printf("PASS (%s): boot requests preserved until first Set; closed-guard Set manages at low; fresh bounds/stock-max; stale worker lowering; mode/ready gates; joined shutdown\n",argc>1?argv[1]:"managed");
    return 0;
}
