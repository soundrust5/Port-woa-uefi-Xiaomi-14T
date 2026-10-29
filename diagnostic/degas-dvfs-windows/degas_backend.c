#include <ntifs.h>
#include "degas_backend.h"
#include "degas_thermal.h"

static PUCHAR gBase;
static ULONG gOpp[3][32],gCount[3],gWanted[3],gCeiling[3],gReasons[3];
static KSPIN_LOCK gLocks[3];
static BOOLEAN gManaged[3];
static volatile LONG gEnabled,gReadFault,gWriteFault;
static KEVENT gStop;
static HANDLE gWorker;
static const ULONG gHighest[3]={2200000,3200000,3350000};
static const ULONG gBoot[3]={1300000,1300000,1200000};
#define LIMIT_THERMAL 1u
static ULONG DomainOffset(ULONG d){return 0x10+d*0x120;}
static ULONG ReadWord(ULONG offset){
#if defined(_WIN32)
    __try{return READ_REGISTER_ULONG((PULONG)(gBase+offset));}
    __except(EXCEPTION_EXECUTE_HANDLER){InterlockedExchange(&gReadFault,1);return 0;}
#else
    return READ_REGISTER_ULONG((PULONG)(gBase+offset));
#endif
}
static BOOLEAN WriteDomain(ULONG d,ULONG offset,ULONG value){
#if defined(_WIN32)
    __try{
#endif
        WRITE_REGISTER_ULONG((PULONG)(gBase+DomainOffset(d)+offset),value);KeMemoryBarrier();
#if defined(_WIN32)
    }__except(EXCEPTION_EXECUTE_HANDLER){InterlockedExchange(&gWriteFault,1);return FALSE;}
#endif
    return TRUE;
}
static BOOLEAN Policy(ULONG d,ULONG *ceiling,LONG *temp){
    BOOLEAN valid=DegasThermalRead(d,ceiling,temp);
    if(!valid)*ceiling=gOpp[d][gCount[d]-1];
    *ceiling=min(*ceiling,gHighest[d]);
    return valid;
}
static VOID ThermalWorker(PVOID Context){
    LARGE_INTEGER delay;ULONG d,cap,reasons,request;LONG temp;KIRQL old;
    BOOLEAN valid,changed;UNREFERENCED_PARAMETER(Context);delay.QuadPart=-2000000;
    for(;;){
        DegasThermalPoll();
        if(InterlockedCompareExchange(&gEnabled,0,0))for(d=0;d<3;d++){
            KeAcquireSpinLock(&gLocks[d],&old);
            valid=Policy(d,&cap,&temp);reasons=(!valid || cap<gHighest[d])?LIMIT_THERMAL:0;
            changed=cap!=gCeiling[d] || reasons!=gReasons[d];gCeiling[d]=cap;gReasons[d]=reasons;
            request=ReadWord(DomainOffset(d)+0x88);
            if(request<gCount[d])request=gOpp[d][request];
            // The worker only lowers a request. Raising follows an OS PERF_SET.
            if(ReadWord(0x4cc)!=1 || (ReadWord(DomainOffset(d)+0x8c)&3)!=3 || gReadFault || gWriteFault){
                InterlockedExchange(&gEnabled,0);changed=TRUE;
            }else if(gManaged[d] && request>cap)WriteDomain(d,0x88,cap);
            KeReleaseSpinLock(&gLocks[d],old);
            if(changed)DegasPepNotifyConstraints(d);
        }
        if(KeWaitForSingleObject(&gStop,Executive,KernelMode,FALSE,&delay)==STATUS_SUCCESS)break;
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}
NTSTATUS DegasBackendInitialize(DEGAS_BACKEND_INFO *Info){
    PHYSICAL_ADDRESS pa;ULONG d,i,previous,value;NTSTATUS status;OBJECT_ATTRIBUTES attributes;
    if(!Info || gBase)return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Info,sizeof(*Info));pa.QuadPart=0x0011bc00;
    gBase=(PUCHAR)MmMapIoSpaceEx(pa,0x1400,PAGE_READWRITE|PAGE_NOCACHE);
    if(!gBase)return STATUS_INSUFFICIENT_RESOURCES;
    if(ReadWord(0x4cc)!=1)return STATUS_NOT_SUPPORTED;
    for(d=0;d<3;d++){
        KeInitializeSpinLock(&gLocks[d]);previous=0;
        for(i=0;i<32;i++){
            value=(ReadWord(DomainOffset(d)+i*4)&0xfff)*1000;
            if(value==previous)break;
            if(!value || (i && value>=previous))return STATUS_DATA_ERROR;
            gOpp[d][gCount[d]++]=value;previous=value;
        }
        if(gCount[d]<2 || gOpp[d][0]!=gHighest[d] ||
           (ReadWord(DomainOffset(d)+0x8c)&3)!=3 || gReadFault)return STATUS_DEVICE_NOT_READY;
        Info->Domains[d].LowestKhz=gOpp[d][gCount[d]-1];
        Info->Domains[d].LowestNonlinearKhz=Info->Domains[d].LowestKhz;
        Info->Domains[d].NominalKhz=gBoot[d];Info->Domains[d].HighestKhz=gHighest[d];
        Info->Domains[d].TransitionLatency100ns=100000;Info->Domains[d].TransitionOverhead100ns=100000;
        gWanted[d]=gBoot[d];gCeiling[d]=Info->Domains[d].LowestKhz;gReasons[d]=LIMIT_THERMAL;
    }
    status=DegasThermalInitialize();if(!NT_SUCCESS(status))return status;
    // Only the installed stock driver's enable1 handshake at PASSIVE_LEVEL.
    for(d=0;d<3;d++){
        if(!WriteDomain(d,0x84,1))return STATUS_ACCESS_VIOLATION;
        for(i=0;i<300;i++){
            if((ReadWord(DomainOffset(d)+0x8c)&3)==3)break;
            {LARGE_INTEGER delay;delay.QuadPart=-10000;KeDelayExecutionThread(KernelMode,FALSE,&delay);}
        }
        if(i==300 || gReadFault)return STATUS_DEVICE_NOT_READY;
    }
    KeInitializeEvent(&gStop,NotificationEvent,FALSE);
    InitializeObjectAttributes(&attributes,NULL,OBJ_KERNEL_HANDLE,NULL,NULL);
    status=PsCreateSystemThread(&gWorker,SYNCHRONIZE,&attributes,NULL,NULL,ThermalWorker,NULL);
    if(!NT_SUCCESS(status))return status;
    InterlockedExchange(&gEnabled,1);return STATUS_SUCCESS;
}
BOOLEAN DegasBackendReadSnapshot(ULONG d,DEGAS_DOMAIN_SNAPSHOT *s){
    ULONG request,cap=0;LONG temp=0;BOOLEAN valid;
    if(!s || d>=3)return FALSE;RtlZeroMemory(s,sizeof(*s));
    if(!gBase || !gCount[d])return FALSE;
    request=ReadWord(DomainOffset(d)+0x88);if(request<gCount[d])request=gOpp[d][request];
    s->RequestKhz=request;s->Ready=ReadWord(0x4cc)==1 && (ReadWord(DomainOffset(d)+0x8c)&3)==3 && !gReadFault && !gWriteFault;
    valid=Policy(d,&cap,&temp);s->ThermalValid=valid;s->TemperatureMilliC=temp;
    s->CeilingKhz=cap;s->LimitReasons=(!valid || cap<gHighest[d])?LIMIT_THERMAL:0;
    s->ThermalStatus=valid?STATUS_SUCCESS:STATUS_DEVICE_NOT_READY;return !gReadFault;
}
BOOLEAN DegasBackendSetDomainKhz(ULONG d,ULONG desired,ULONG minimum,ULONG maximum){
    ULONG cap=0,lo,hi,target;LONG temp=0;BOOLEAN valid,result;
    if(d>=3 || !gBase || !gCount[d] || !gEnabled || gReadFault || gWriteFault || minimum>maximum)return FALSE;
    lo=max(minimum,gOpp[d][gCount[d]-1]);hi=min(maximum,gHighest[d]);if(lo>hi)return FALSE;
    KeAcquireSpinLockAtDpcLevel(&gLocks[d]);
    if(!InterlockedCompareExchange(&gEnabled,0,0) || ReadWord(0x4cc)!=1 || (ReadWord(DomainOffset(d)+0x8c)&3)!=3 || gReadFault || gWriteFault){
        InterlockedExchange(&gEnabled,0);KeReleaseSpinLockFromDpcLevel(&gLocks[d]);return FALSE;
    }
    valid=Policy(d,&cap,&temp);
    // Thermal safety overrides even OS minimum; reported as a thermal limit.
    hi=min(hi,cap);if(lo>hi)lo=hi;target=min(max(desired,lo),hi);
    gWanted[d]=desired;gCeiling[d]=cap;gReasons[d]=(!valid || cap<gHighest[d])?LIMIT_THERMAL:0;
    // Exact Android fast_switch accepts intermediate kHz, not only LUT entries.
    result=WriteDomain(d,0x88,target);
    if(result)gManaged[d]=TRUE;
    KeReleaseSpinLockFromDpcLevel(&gLocks[d]);return result;
}
VOID DegasBackendShutdown(VOID){
    InterlockedExchange(&gEnabled,0);
    if(gWorker){KeSetEvent(&gStop,IO_NO_INCREMENT,FALSE);ZwWaitForSingleObject(gWorker,FALSE,NULL);ZwClose(gWorker);gWorker=NULL;}
    DegasThermalShutdown();if(gBase){MmUnmapIoSpace(gBase,0x1400);gBase=NULL;}
}
