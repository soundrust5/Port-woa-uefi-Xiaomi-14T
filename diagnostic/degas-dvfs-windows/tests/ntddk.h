/* Host-only shim for executing the actual backend C with fake shared memory.
 * This directory is never an include path in an ARM64 driver build. */
#ifndef DEGAS_TEST_NTDDK_H
#define DEGAS_TEST_NTDDK_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define _IRQL_requires_(x)
#define _IRQL_requires_max_(x)
#define _In_
#define _Out_
#define PASSIVE_LEVEL 0
#define DISPATCH_LEVEL 2
#define TRUE 1
#define FALSE 0
#define PAGE_READWRITE 4
#define PAGE_READONLY 2
#define PAGE_NOCACHE 0x200
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xc000000d)
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xc000009a)
#define STATUS_NOT_SUPPORTED ((NTSTATUS)0xc00000bb)
#define STATUS_DATA_ERROR ((NTSTATUS)0xc000003e)
#define STATUS_DEVICE_NOT_READY ((NTSTATUS)0xc00000a3)
#define STATUS_ACCESS_VIOLATION ((NTSTATUS)0xc0000005)
#define STATUS_TIMEOUT ((NTSTATUS)0x102)
#define NT_SUCCESS(s) ((s) >= 0)
#define RtlZeroMemory(p,n) memset((p),0,(n))
#define DPFLTR_IHVDRIVER_ID 0
#define DPFLTR_WARNING_LEVEL 0
#define UNREFERENCED_PARAMETER(x) (void)(x)
#define min(x,y) ((x)<(y)?(x):(y))
#define KernelMode 0
#define Executive 0
#define NotificationEvent 0
#define OBJ_KERNEL_HANDLE 0x200
#define SYNCHRONIZE 0x100000
#define IO_NO_INCREMENT 0
typedef uint32_t ULONG;
typedef int32_t LONG;
typedef int32_t NTSTATUS;
typedef uint8_t BOOLEAN;
typedef unsigned char *PUCHAR;
typedef ULONG *PULONG;
typedef void VOID;
typedef void *PVOID;
typedef void *HANDLE;
typedef unsigned char KIRQL;
typedef struct { unsigned int Held; } KSPIN_LOCK;
typedef struct { BOOLEAN Signaled; } KEVENT;
typedef struct { ULONG Flags; } OBJECT_ATTRIBUTES;
typedef void (*PKSTART_ROUTINE)(PVOID Context);
typedef struct { int64_t QuadPart; } PHYSICAL_ADDRESS;
typedef struct { int64_t QuadPart; } LARGE_INTEGER;
void *MmMapIoSpaceEx(PHYSICAL_ADDRESS Address, size_t Length, ULONG Protection);
void MmUnmapIoSpace(void *Address, size_t Length);
ULONG READ_REGISTER_ULONG(PULONG Address);
void WRITE_REGISTER_ULONG(PULONG Address, ULONG Value);
void KeMemoryBarrier(void);
int KeDelayExecutionThread(int Mode, int Alertable, LARGE_INTEGER *Delay);
int DbgPrintEx(int Component, int Level, const char *Format, ...);
#define max(x,y) ((x)>(y)?(x):(y))
#define InitializeObjectAttributes(p,n,a,r,s) do { (p)->Flags=(a); } while(0)
LONG InterlockedExchange(volatile LONG *Value, LONG NewValue);
LONG InterlockedCompareExchange(volatile LONG *Value, LONG Exchange, LONG Comparand);
void KeInitializeSpinLock(KSPIN_LOCK *Lock);
void KeAcquireSpinLock(KSPIN_LOCK *Lock, KIRQL *OldIrql);
void KeReleaseSpinLock(KSPIN_LOCK *Lock, KIRQL OldIrql);
void KeAcquireSpinLockAtDpcLevel(KSPIN_LOCK *Lock);
void KeReleaseSpinLockFromDpcLevel(KSPIN_LOCK *Lock);
void KeInitializeEvent(KEVENT *Event, int Type, BOOLEAN State);
LONG KeSetEvent(KEVENT *Event, int Increment, BOOLEAN Wait);
NTSTATUS KeWaitForSingleObject(PVOID Object, int Reason, int Mode, BOOLEAN Alertable,
                               LARGE_INTEGER *Timeout);
NTSTATUS PsCreateSystemThread(HANDLE *Thread, ULONG Access, OBJECT_ATTRIBUTES *Attributes,
    HANDLE Process, PVOID ClientId, PKSTART_ROUTINE Start, PVOID Context);
void PsTerminateSystemThread(NTSTATUS Status);
NTSTATUS ZwWaitForSingleObject(HANDLE Handle, BOOLEAN Alertable, LARGE_INTEGER *Timeout);
NTSTATUS ZwClose(HANDLE Handle);
#endif
