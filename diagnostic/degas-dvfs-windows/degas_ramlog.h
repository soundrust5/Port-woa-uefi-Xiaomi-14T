#ifndef DEGAS_RAMLOG_H
#define DEGAS_RAMLOG_H
#include <ntddk.h>
NTSTATUS DegasRamInitialize(VOID);
VOID DegasRamMark(ULONG Stage,ULONG Event,ULONG_PTR Context);
#endif
