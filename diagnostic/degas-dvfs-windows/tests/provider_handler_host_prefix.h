#include <assert.h>
#include <stdint.h>
typedef uintptr_t ULONG_PTR;
static void DegasRamMark(unsigned long stage, unsigned long event, ULONG_PTR context)
{ (void)stage; (void)event; (void)context; }
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
typedef uint32_t ULONG;
typedef int32_t LONG;
typedef uint8_t BOOLEAN;
typedef uint16_t WCHAR;
typedef unsigned char KIRQL;
typedef void *PVOID;
typedef void *PEPHANDLE;
typedef void *POHANDLE;
typedef unsigned int KSPIN_LOCK;
typedef struct { uint16_t Length, MaximumLength; WCHAR *Buffer; } UNICODE_STRING;
typedef const UNICODE_STRING *PCUNICODE_STRING;
typedef struct { unsigned char Bytes[16]; } GUID;
#define TRUE 1
#define FALSE 0
#define NULL_HANDLE ((void *)0)
#define STATUS_NOT_SUPPORTED ((LONG)0xc00000bb)
#define RTL_NUMBER_OF(x) (sizeof(x)/sizeof((x)[0]))
#define DPFLTR_IHVDRIVER_ID 0
#define DPFLTR_INFO_LEVEL 0
typedef enum { PepDeviceNotAccepted=0, PepDeviceAcceptedReserved=1, PepDeviceAccepted=2 } PEP_DEVICE_ACCEPTANCE_TYPE;
typedef struct { PCUNICODE_STRING DeviceId; BOOLEAN DeviceAccepted; } PEP_PREPARE_DEVICE,*PPEP_PREPARE_DEVICE;
typedef PEP_PREPARE_DEVICE *PPEP_ABANDON_DEVICE;
typedef struct { PCUNICODE_STRING DeviceId; POHANDLE KernelHandle; PVOID Register; PEPHANDLE DeviceHandle; PEP_DEVICE_ACCEPTANCE_TYPE DeviceAccepted; } PEP_REGISTER_DEVICE,*PPEP_REGISTER_DEVICE;
typedef struct { PEPHANDLE DeviceHandle; } PEP_UNREGISTER_DEVICE,*PPEP_UNREGISTER_DEVICE;
typedef PEP_UNREGISTER_DEVICE *PPEP_DEVICE_STARTED;
typedef struct { PEPHANDLE DeviceHandle; ULONG PowerState; BOOLEAN Complete,SystemTransition; } PEP_DEVICE_POWER_STATE;
typedef struct { POHANDLE DeviceHandle; const GUID *PowerControlCode; PVOID RequestContext; PVOID InBuffer; size_t InBufferSize; PVOID OutBuffer; size_t OutBufferSize; } PEP_WORK_POWER_CONTROL;
typedef struct { ULONG WorkType; PEP_WORK_POWER_CONTROL PowerControl; } PEP_WORK_INFORMATION;
typedef struct { PEP_WORK_INFORMATION *WorkInformation; BOOLEAN NeedWork; } PEP_WORK,*PPEP_WORK;
#define PepWorkRequestPowerControl 3
#define RtlZeroMemory(p,n) memset((p),0,(n))
typedef struct { PEPHANDLE DeviceHandle; ULONG Component; BOOLEAN Active; PVOID WorkInformation; BOOLEAN NeedWork; } PEP_COMPONENT_ACTIVE,*PPEP_COMPONENT_ACTIVE;
typedef struct { PEPHANDLE DeviceHandle; ULONG Component,IdleState; BOOLEAN DriverNotified,Completed; } PEP_NOTIFY_COMPONENT_IDLE_STATE,*PPEP_NOTIFY_COMPONENT_IDLE_STATE;
typedef struct { PEPHANDLE DeviceHandle; const GUID *PowerControlCode; PVOID InBuffer; size_t InBufferSize; PVOID OutBuffer; size_t OutBufferSize; size_t BytesReturned; LONG Status; } PEP_POWER_CONTROL_REQUEST,*PPEP_POWER_CONTROL_REQUEST;
typedef struct { PEPHANDLE DeviceHandle; const GUID *PowerControlCode; PVOID RequestContext; size_t BytesReturned; LONG Status; } PEP_POWER_CONTROL_COMPLETE,*PPEP_POWER_CONTROL_COMPLETE;
typedef struct { ULONG Uid,Domain; volatile LONG Registered,Started,ConstraintPending,ConstraintInFlight; POHANDLE KernelHandle; } DEGAS_CPU;
static DEGAS_CPU gCpus[8];
static volatile LONG gDpmPrepareCount,gDpmRegisterCount,gDomainFault[3];
static KSPIN_LOCK gDiagnosticLock;
static ULONG gLastAcceptedUid=UINT32_MAX;
static GUID gConstraintGuid;
static unsigned int rejected,notified;
static LONG InterlockedIncrement(volatile LONG *p) { return ++*p; }
static LONG InterlockedExchange(volatile LONG *p,LONG x) { LONG old=*p;*p=x;return old; }
static LONG InterlockedCompareExchange(volatile LONG *p,LONG x,LONG c) { LONG old=*p;if(old==c)*p=x;return old; }
static void KeAcquireSpinLock(KSPIN_LOCK *p,KIRQL *old) { assert(!*p);*p=1;*old=0; }
static void KeReleaseSpinLock(KSPIN_LOCK *p,KIRQL old) { (void)old;assert(*p);*p=0; }
static int DbgPrintEx(int c,int l,const char *s,...) { (void)c;(void)l;(void)s;return 0; }
static size_t RtlCompareMemory(const void *a,const void *b,size_t n) { return memcmp(a,b,n)==0?n:0; }
static void DegasRememberRejected(PCUNICODE_STRING id) { (void)id;++rejected; }
static void DegasPepNotifyConstraints(ULONG domain) { assert(domain<3);++notified; }
static void DegasRequestConstraintWorker(void) { ++notified; }
#include "../degas_protocol.h"
