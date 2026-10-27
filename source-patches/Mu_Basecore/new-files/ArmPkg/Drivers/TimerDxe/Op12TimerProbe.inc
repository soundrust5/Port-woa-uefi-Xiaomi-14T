// Included only in the opt-in OP12 Windows firmware. The existing timer
// handler owns both PPIs; do not replace another driver's interrupt handler.
#include <Protocol/Op12TimerProbe.h>
STATIC volatile UINT64 mOp12VirtualIrqs, mOp12PhysicalIrqs, mOp12PhysicalSource;
STATIC volatile BOOLEAN mOp12PhysicalProbe;
#ifndef OP12_TIMER_HOST_TEST
#define OP12_READ(Reg, Value) __asm__ volatile ("isb; mrs %0, " #Reg : "=r"(Value))
#define OP12_WRITE(Reg, Value) __asm__ volatile ("msr " #Reg ", %0; isb" :: "r"((UINT64)(Value)) : "memory")
#endif

// Called from the actual GIC-dispatched handler, not a software timer event.
STATIC BOOLEAN Op12TimerObserve (HARDWARE_INTERRUPT_SOURCE Source)
{
  if (Source == 27) mOp12VirtualIrqs++;
  if (Source == 30 && mOp12PhysicalProbe) {
    OP12_WRITE(cntp_ctl_el0, 2); // mask/deassert source before EOI
    mOp12PhysicalSource = Source;
    mOp12PhysicalIrqs++;
    gInterrupt->EndOfInterrupt (gInterrupt, Source);
    return TRUE;
  }
  return FALSE;
}

STATIC VOID Op12TimerWait (UINT64 Ticks)
{
  UINT64 Start, Now;
  OP12_READ(cntpct_el0, Start);
  // Counter deadline AND finite iteration bound; never depend on IRQ progress.
  for (UINTN I=0; I<10000000; I++) {
    OP12_READ(cntpct_el0, Now);
    if (Now-Start >= Ticks) break;
  }
}

STATIC EFI_STATUS EFIAPI Op12TimerRun (OP12_TIMER_RESULT *R)
{
  EFI_STATUS S;
  BOOLEAN Enabled, After=FALSE;
  EFI_TPL Tpl;
  UINT64 El, Now, Ctl, Compare;
  if (R == NULL) return EFI_INVALID_PARAMETER;
  ZeroMem (R, sizeof(*R)); R->Version=1;
  R->VirtualStatus=R->PhysicalStatus=EFI_NOT_STARTED;
  R->RestoreStatus=EFI_NOT_STARTED;
  OP12_READ(CurrentEL, El); OP12_READ(DAIF, R->Daif);
  if (El != 4 || (R->Daif & 0x80) != 0 || mOp12PhysicalProbe)
    return EFI_UNSUPPORTED;
  Tpl=gBS->RaiseTPL(TPL_HIGH_LEVEL); gBS->RestoreTPL(Tpl);
  if (Tpl > TPL_CALLBACK) return EFI_UNSUPPORTED;
  if (PcdGet32(PcdArmArchTimerVirtIntrNum)!=27 || PcdGet32(PcdArmArchTimerIntrNum)!=30)
    return EFI_UNSUPPORTED;
  OP12_READ(cntfrq_el0, R->Frequency);
  if (R->Frequency != 19200000) return EFI_UNSUPPORTED;
  OP12_READ(icc_pmr_el1, R->Pmr); OP12_READ(icc_igrpen1_el1, R->Group1);
  R->VirtualBefore=mOp12VirtualIrqs;
  Op12TimerWait(R->Frequency/10); // 100ms of normal virtual timer delivery
  R->VirtualAfter=mOp12VirtualIrqs;
  R->VirtualStatus=R->VirtualAfter>R->VirtualBefore ? EFI_SUCCESS : EFI_TIMEOUT;
  OP12_READ(cntp_ctl_el0, Ctl); OP12_READ(cntp_cval_el0, Compare);
  R->PhysicalCtlBefore=Ctl; R->PhysicalCompareBefore=Compare;
  // Refuse to borrow an enabled physical timer, even if it is masked.
  if (Ctl & 1) return EFI_ACCESS_DENIED;
  S=gInterrupt->GetInterruptSourceState(gInterrupt,30,&Enabled);
  if (EFI_ERROR(S)) return S;
  R->PhysicalEnabledBefore=Enabled;
  mOp12PhysicalIrqs=0; mOp12PhysicalSource=0;
  mOp12PhysicalProbe=TRUE;
  if (!Enabled) {
    S=gInterrupt->EnableInterruptSource(gInterrupt,30);
    if (EFI_ERROR(S)) { mOp12PhysicalProbe=FALSE; return S; }
  }
  OP12_READ(cntpct_el0, Now);
  OP12_WRITE(cntp_cval_el0, Now+R->Frequency/200); // one IRQ after 5ms
  OP12_WRITE(cntp_ctl_el0, 1);
  Op12TimerWait(R->Frequency/20); // max50ms, independent of IRQ
  OP12_READ(cntp_ctl_el0, R->PhysicalCtlExpired);
  // Mask IRQ while restoring control and releasing the diagnostic handler.
  Tpl=gBS->RaiseTPL(TPL_HIGH_LEVEL);
  OP12_WRITE(cntp_ctl_el0, 2);
  R->PhysicalCount=mOp12PhysicalIrqs;
  R->LastPhysicalSource=mOp12PhysicalSource;
  S=Enabled ? EFI_SUCCESS : gInterrupt->DisableInterruptSource(gInterrupt,30);
  OP12_WRITE(cntp_cval_el0, Compare);
  OP12_WRITE(cntp_ctl_el0, Ctl & 3);
  mOp12PhysicalProbe=FALSE;
  gBS->RestoreTPL(Tpl);
  OP12_READ(cntp_ctl_el0, R->PhysicalCtlAfter);
  OP12_READ(cntp_cval_el0, R->PhysicalCompareAfter);
  R->RestoreStatus=S;
  S=gInterrupt->GetInterruptSourceState(gInterrupt,30,&After);
  R->PhysicalEnabledAfter=After;
  if (EFI_ERROR(S) || EFI_ERROR(R->RestoreStatus) || After!=Enabled ||
      (R->PhysicalCtlAfter&3)!=(Ctl&3) || R->PhysicalCompareAfter!=Compare)
    R->RestoreStatus=EFI_DEVICE_ERROR;
  R->PhysicalStatus=R->PhysicalCount==1 && R->LastPhysicalSource==30 ? EFI_SUCCESS : EFI_TIMEOUT;
  if (EFI_ERROR(R->RestoreStatus)) return R->RestoreStatus;
  return EFI_ERROR(R->VirtualStatus) ? R->VirtualStatus : R->PhysicalStatus;
}
STATIC OP12_TIMER_PROBE mOp12TimerProbe = { Op12TimerRun };
STATIC EFI_GUID mOp12TimerProbeGuid = OP12_TIMER_PROBE_GUID;
