#ifndef DEGAS_AMU_H
#define DEGAS_AMU_H
#include "degas_ioctl.h"
/* Probe results, independent of NTSTATUS. Zero means qualified sample only. */
#define DEGAS_AMU_OK 0u
#define DEGAS_AMU_UNSUPPORTED 1u
#define DEGAS_AMU_DISABLED 2u
#define DEGAS_AMU_EXCEPTION 3u
#define DEGAS_AMU_BAD_REFERENCE 4u
#define DEGAS_AMU_NO_ADVANCE 5u
#define DEGAS_AMU_REFERENCE_MISMATCH 6u
#define DEGAS_AMU_OVERFLOW 7u
#define DEGAS_AMU_BAD_IDENTITY 8u
#define DEGAS_AMU_BAD_TOPOLOGY 9u
#define DEGAS_AMU_NOT_PROBED 10u
#ifdef _KERNEL_MODE
NTSTATUS DegasAmuInitialize(VOID);
VOID DegasAmuReadSnapshot(DEGAS_AMU_STATE *Output);
#endif
#endif
