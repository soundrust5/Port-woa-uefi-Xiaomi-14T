#include "degas_vhf.h"

typedef struct { DEGAS_TOUCH_VHF *Owner; } DEGAS_TIMER_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEGAS_TIMER_CONTEXT, DegasGetTimerContext)
static EVT_WDF_TIMER DegasTouchTimer;
static EVT_VHF_ASYNC_OPERATION DegasTouchGetFeature;

static uint64_t DegasTouchNow(void)
{ return KeQueryInterruptTime() / 10000u; }

/* VHF default buffering copies the report before returning. No ready callback
 * is registered, so the stack-local byte buffer may be reused after this call.
 * Core updates host state only on success; failed submissions latch a fault. */
static int DegasTouchSink(void *Context, const uint8_t *Report, size_t Size)
{
  DEGAS_TOUCH_VHF *D = (DEGAS_TOUCH_VHF *)Context;
  HID_XFER_PACKET Packet = {0};
  Packet.reportId = DEGAS_HID_INPUT_ID;
  Packet.reportBuffer = (PUCHAR)Report;
  Packet.reportBufferLen = (ULONG)Size;
  D->LastSubmitStatus = VhfReadReportSubmit(D->Vhf, &Packet);
  return NT_SUCCESS(D->LastSubmitStatus);
}

static VOID DegasTouchGetFeature(PVOID Context, VHFOPERATIONHANDLE Operation,
  PVOID OperationContext, PHID_XFER_PACKET Packet)
{
  NTSTATUS Status = STATUS_NOT_SUPPORTED;
  UNREFERENCED_PARAMETER(Context);
  UNREFERENCED_PARAMETER(OperationContext);
  /* May execute at DISPATCH_LEVEL. Immutable nonpaged data only; no wait lock. */
  if (Packet && Packet->reportId == DEGAS_HID_FEATURE_ID) {
    if (Packet->reportBuffer && Packet->reportBufferLen == 2) {
      Packet->reportBuffer[0] = DEGAS_HID_FEATURE_ID;
      Packet->reportBuffer[1] = DEGAS_HID_MAX_CONTACTS;
      Status = STATUS_SUCCESS;
    } else Status = STATUS_INVALID_BUFFER_SIZE;
  }
  (void)VhfAsyncOperationComplete(Operation, Status);
}

static VOID DegasTouchTimer(WDFTIMER Timer)
{
  DEGAS_TOUCH_VHF *D = DegasGetTimerContext(Timer)->Owner;
  (void)WdfWaitLockAcquire(D->Lock, NULL);
  if (D->Running) {
    (void)DegasHidTick(&D->State, DegasTouchNow(), DegasTouchSink, D);
    /* Passive KMDF timers cannot be periodic; explicitly rearm a one-shot. */
    (void)WdfTimerStart(Timer, WDF_REL_TIMEOUT_IN_MS(10));
  }
  WdfWaitLockRelease(D->Lock);
}

NTSTATUS DegasTouchVhfCreate(DEGAS_TOUCH_VHF *D, WDFDEVICE Device)
{
  WDF_OBJECT_ATTRIBUTES Attributes;
  WDF_TIMER_CONFIG TimerConfig;
  VHF_CONFIG VhfConfig;
  NTSTATUS Status;
  static WCHAR HardwareIds[]=L"DEGAS\\TOUCH_VHF\0";
  PAGED_CODE();
  if (!D || !Device) return STATUS_INVALID_PARAMETER;
  if (D->Lock || D->Timer || D->Vhf || D->Running) return STATUS_INVALID_DEVICE_STATE;
  RtlZeroMemory(&D->State, sizeof(D->State));
  D->State.faulted = 1;
  RtlCopyMemory(D->Descriptor, DegasHidDescriptor, sizeof(D->Descriptor));
  D->LastSubmitStatus = STATUS_SUCCESS;
  WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
  Attributes.ParentObject = Device;
  Status = WdfWaitLockCreate(&Attributes, &D->Lock);
  if (!NT_SUCCESS(Status)) goto Fail;
  WDF_TIMER_CONFIG_INIT(&TimerConfig, DegasTouchTimer);
  TimerConfig.AutomaticSerialization = FALSE;
  WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, DEGAS_TIMER_CONTEXT);
  Attributes.ParentObject = Device;
  Attributes.ExecutionLevel = WdfExecutionLevelPassive;
  Attributes.SynchronizationScope = WdfSynchronizationScopeNone;
  Status = WdfTimerCreate(&TimerConfig, &Attributes, &D->Timer);
  if (!NT_SUCCESS(Status)) goto Fail;
  DegasGetTimerContext(D->Timer)->Owner = D;
  VHF_CONFIG_INIT(&VhfConfig, WdfDeviceWdmGetDeviceObject(Device),
    (USHORT)sizeof(D->Descriptor), D->Descriptor);
  VhfConfig.VhfClientContext = D;
  /* Exact project ID avoids the PE inbox Include/Needs dependency failure.
   * MULTI_SZ length is in bytes and includes both trailing wide NULs. */
  VhfConfig.HardwareIDs=HardwareIds;
  VhfConfig.HardwareIDsLength=(USHORT)sizeof(HardwareIds);
  VhfConfig.EvtVhfAsyncOperationGetFeature = DegasTouchGetFeature;
  /* No borrowed vendor/product identity, certification blob or firmware I/O. */
  Status = VhfCreate(&VhfConfig, &D->Vhf);
  if (!NT_SUCCESS(Status)) goto Fail;
  Status = VhfStart(D->Vhf);
  if (!NT_SUCCESS(Status)) goto Fail;
  D->Running = TRUE;
  (void)WdfTimerStart(D->Timer, WDF_REL_TIMEOUT_IN_MS(10));
  return STATUS_SUCCESS;
Fail:
  DegasTouchVhfDestroy(D);
  return Status;
}

DEGAS_HID_RESULT DegasTouchVhfSubmit(DEGAS_TOUCH_VHF *D, const DEGAS_FTS_REPORT *Snapshot)
{
  DEGAS_HID_RESULT Result;
  PAGED_CODE();
  if (!D || !D->Lock) return DegasHidInvalid;
  (void)WdfWaitLockAcquire(D->Lock, NULL);
  Result = D->Running ? DegasHidUpdate(&D->State, Snapshot, DegasTouchNow(), DegasTouchSink, D) : DegasHidFaulted;
  WdfWaitLockRelease(D->Lock);
  return Result;
}

DEGAS_HID_RESULT DegasTouchVhfFault(DEGAS_TOUCH_VHF *D)
{
  DEGAS_HID_RESULT Result;
  PAGED_CODE();
  if (!D || !D->Lock) return DegasHidInvalid;
  (void)WdfWaitLockAcquire(D->Lock, NULL);
  Result = D->Running ? DegasHidFault(&D->State, DegasTouchSink, D) : DegasHidFaulted;
  WdfWaitLockRelease(D->Lock);
  return Result;
}

BOOLEAN DegasTouchVhfRearm(DEGAS_TOUCH_VHF *D)
{
  BOOLEAN Result;
  PAGED_CODE();
  if (!D || !D->Lock) return FALSE;
  (void)WdfWaitLockAcquire(D->Lock, NULL);
  Result = D->Running && DegasHidRearm(&D->State);
  WdfWaitLockRelease(D->Lock);
  return Result;
}

VOID DegasTouchVhfDestroy(DEGAS_TOUCH_VHF *D)
{
  PAGED_CODE();
  if (!D) return;
  /* Owner must stop/join its SPI worker before entering, and must not hold
   * parent device synchronization locks that a callback could require. */
  if (D->Lock) {
    (void)WdfWaitLockAcquire(D->Lock, NULL);
    if (D->Running) (void)DegasHidFault(&D->State, DegasTouchSink, D);
    D->Running = FALSE;
    WdfWaitLockRelease(D->Lock);
  }
  if (D->Timer) (void)WdfTimerStop(D->Timer, TRUE);
  /* Wait outside the lock; this drains VHF feature callbacks before clearing. */
  if (D->Vhf) VhfDelete(D->Vhf, TRUE);
  if (D->Timer) WdfObjectDelete(D->Timer);
  if (D->Lock) WdfObjectDelete(D->Lock);
  RtlZeroMemory(D, sizeof(*D));
}
