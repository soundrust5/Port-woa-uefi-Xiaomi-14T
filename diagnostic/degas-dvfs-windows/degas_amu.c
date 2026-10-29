/* Boot-only, read-only AMU qualification. Never advertises PEP feedback. */
#include <ntddk.h>
#include <intrin.h>
#include "degas_amu.h"
#include "degas_amu_math.h"
static DEGAS_AMU_STATE gAmu;

static VOID DegasAmuSample(DEGAS_AMU_CPU_STATE *p)
{
    ULONG attempt;
    __try {
        p->Mpidr = _ReadStatusReg(ARM64_SYSREG(3,0,0,0,5));
        p->Pfr0 = _ReadStatusReg(ARM64_SYSREG(3,0,0,4,0));
        /* MT6897 CPU UID is Aff1. Ignore architectural U/MT/RES1 flags only. */
        if (!DegasAmuValidateIdentity(p)) {
            p->Status = DEGAS_AMU_BAD_IDENTITY;
            return;
        }
        if (!DegasAmuFeatureSupported(p->Pfr0)) {
            p->Status = DEGAS_AMU_UNSUPPORTED;
            return;
        }
        p->Enable = _ReadStatusReg(ARM64_SYSREG(3,3,13,2,5));
        p->ReferenceHz = _ReadStatusReg(ARM64_SYSREG(3,3,14,0,0));
        if ((p->Enable & 3) != 3) {
            p->Status = DEGAS_AMU_DISABLED;
            return;
        }
        for (attempt = 0; attempt < 3; ++attempt) {
            p->Attempts = attempt + 1;
            __isb(_ARM64_BARRIER_SY);
            p->TimerBefore = _ReadStatusReg(ARM64_SYSREG(3,3,14,0,2));
            p->CoreBefore = _ReadStatusReg(ARM64_SYSREG(3,3,13,4,0));
            p->ConstantBefore = _ReadStatusReg(ARM64_SYSREG(3,3,13,4,1));
            KeStallExecutionProcessor(1000);
            __isb(_ARM64_BARRIER_SY);
            p->ConstantAfter = _ReadStatusReg(ARM64_SYSREG(3,3,13,4,1));
            p->CoreAfter = _ReadStatusReg(ARM64_SYSREG(3,3,13,4,0));
            p->TimerAfter = _ReadStatusReg(ARM64_SYSREG(3,3,14,0,2));
            p->Status = DegasAmuValidate(p);
            if (p->Status == DEGAS_AMU_OK) return;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        p->Status = DEGAS_AMU_EXCEPTION;
        p->MeasuredKhz = 0;
    }
}

NTSTATUS DegasAmuInitialize(VOID)
{
    ULONG i, seen = 0;
    RtlZeroMemory(&gAmu, sizeof(gAmu));
    gAmu.InitStatus = STATUS_NOT_SUPPORTED;
    for (i = 0; i < 8; ++i) {
        gAmu.Cpus[i].Status = DEGAS_AMU_NOT_PROBED;
        gAmu.Cpus[i].Uid = MAXULONG;
    }
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        gAmu.InitStatus = STATUS_INVALID_DEVICE_STATE;
        gAmu.ProbeComplete = 1;
        return gAmu.InitStatus;
    }
    gAmu.ProcessorCount = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    if (gAmu.ProcessorCount != 8) {
        gAmu.InitStatus = STATUS_DEVICE_CONFIGURATION_ERROR;
        gAmu.ProbeComplete = 1;
        return gAmu.InitStatus;
    }
    for (i = 0; i < 8; ++i) {
        PROCESSOR_NUMBER number;
        GROUP_AFFINITY target, previous;
        DEGAS_AMU_CPU_STATE *p = &gAmu.Cpus[i];
        if (!NT_SUCCESS(KeGetProcessorNumberFromIndex(i, &number))) {
            p->Status = DEGAS_AMU_BAD_TOPOLOGY;
            continue;
        }
        RtlZeroMemory(&target, sizeof(target));
        target.Group = number.Group;
        target.Mask = (KAFFINITY)1 << number.Number;
        KeSetSystemGroupAffinityThread(&target, &previous);
        __try {
            DegasAmuSample(p);
        } __finally {
            KeRevertToUserGroupAffinityThread(&previous);
        }
        if (p->Uid < 8) {
            ULONG bit = 1u << p->Uid;
            if (seen & bit) p->Status = DEGAS_AMU_BAD_IDENTITY;
            seen |= bit;
            if (p->Status == DEGAS_AMU_OK) gAmu.QualifiedMask |= bit;
        }
    }
    if (seen != 255) gAmu.QualifiedMask = 0;
    gAmu.InitStatus = gAmu.QualifiedMask == 255 ? STATUS_SUCCESS : STATUS_NOT_SUPPORTED;
    gAmu.ProbeComplete = 1;
    return gAmu.InitStatus;
}

VOID DegasAmuReadSnapshot(DEGAS_AMU_STATE *Output)
{
    /* Published by provider's initialization-complete barrier; immutable later. */
    RtlCopyMemory(Output, &gAmu, sizeof(*Output));
}
