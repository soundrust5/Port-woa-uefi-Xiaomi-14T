// No Boot Services, console, disk IO or allocation in the trace writer.
#if defined(OP12_WINDOWS_BOOT) && OP12_WINDOWS_BOOT && defined(MDE_CPU_AARCH64)
#include "Op12TraceFormat.h"
#include <Library/BaseMemoryLib.h>
STATIC volatile OP12_TRACE_WORD *mOp12Trace;
STATIC UINT64 mOp12TraceKepBase;
STATIC VOID Op12TraceMark (UINT64 Stage, UINT64 Detail, UINT64 Status)
{
  int Last;
  if (mOp12Trace == NULL) return;
  Last = Op12TraceLatest (mOp12Trace);
  if (Last < 0) return;
  Op12TraceStore (mOp12Trace, mOp12Trace[Last+2], Stage, Detail, Status);
  WriteBackDataCacheRange ((VOID *)(UINTN)mOp12Trace, OP12_TRACE_BYTES);
  __asm__ volatile ("dsb sy" ::: "memory");
}
STATIC VOID Op12TraceLocate (VOID)
{
  CONST EFI_GUID Guid = OP12_TRACE_GUID;
  for (UINTN I=0; I<gST->NumberOfTableEntries; I++) {
    if (CompareGuid (&gST->ConfigurationTable[I].VendorGuid, &Guid) &&
        (UINTN)gST->ConfigurationTable[I].VendorTable == OP12_TRACE_ADDRESS) {
      mOp12Trace=(VOID *)(UINTN)OP12_TRACE_ADDRESS;
      int Last=Op12TraceLatest(mOp12Trace);
      if (Last>=0 && (mOp12Trace[Last+3]==0x61 || mOp12Trace[Last+3]==0x63) &&
          mOp12Trace[Last+5]==0)
        mOp12TraceKepBase=mOp12Trace[Last+4];
      break;
    }
  }
}
#else
#define Op12TraceMark(Stage, Detail, Status) ((VOID)0)
#define Op12TraceLocate() ((VOID)0)
#define mOp12TraceKepBase 0
#endif
