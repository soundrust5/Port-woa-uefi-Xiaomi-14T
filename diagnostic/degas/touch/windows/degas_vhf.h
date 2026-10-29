/* All public APIs PASSIVE_LEVEL; owner must stop/join producer before Destroy.
 * Context and immutable descriptor must remain nonpaged until Destroy returns.
 */
#ifndef DEGAS_VHF_H
#define DEGAS_VHF_H
#include <ntddk.h>
#include <wdf.h>
#include <vhf.h>
#include "degas_hid.h"
typedef struct {
 DEGAS_HID_STATE State;
 uint8_t Descriptor[sizeof(DegasHidDescriptor)];
 WDFWAITLOCK Lock;
 WDFTIMER Timer;
 VHFHANDLE Vhf;
 NTSTATUS LastSubmitStatus;
 BOOLEAN Running;
} DEGAS_TOUCH_VHF;
/* Fresh zeroed storage, VHF lower filter required. Starts faulted. */
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS DegasTouchVhfCreate(DEGAS_TOUCH_VHF *D,WDFDEVICE Device);
_IRQL_requires_(PASSIVE_LEVEL)
DEGAS_HID_RESULT DegasTouchVhfSubmit(DEGAS_TOUCH_VHF *D,const DEGAS_FTS_REPORT *Report);
_IRQL_requires_(PASSIVE_LEVEL)
DEGAS_HID_RESULT DegasTouchVhfFault(DEGAS_TOUCH_VHF *D);
/* After hardware ID/mode revalidated; pending release must be accepted first. */
_IRQL_requires_(PASSIVE_LEVEL)
BOOLEAN DegasTouchVhfRearm(DEGAS_TOUCH_VHF *D);
_IRQL_requires_(PASSIVE_LEVEL)
VOID DegasTouchVhfDestroy(DEGAS_TOUCH_VHF *D);
#endif
