/**
  Copyright (c) 2012-2013, ARM Limited. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/PrintLib.h>
#include <Library/SerialPortLib.h>

#include "Sec.h"

#if defined (ONEPLUS12_SEC_PROBE) && ONEPLUS12_SEC_PROBE >= 2
extern VOID Op12FinishSecProbe (UINTN, UINTN, UINTN, UINTN, UINTN);
#endif

VOID
SecCommonExceptionEntry (
  IN UINT32 Type,
  IN UINTN  LR)
{
#if defined (ONEPLUS12_SEC_PROBE) && ONEPLUS12_SEC_PROBE >= 2
  Op12FinishSecProbe (0xEE, 0, 0, Type, LR);
#endif
  CHAR8 Buffer[100];
  UINTN CharCount;

  // Set Exception Message
  switch (Type) {
    case EXCEPT_AARCH64_SYNCHRONOUS_EXCEPTIONS:
      CharCount = AsciiSPrint (Buffer, sizeof (Buffer), "Synchronous Exception at 0x%llx\n", LR);
      break;

    case EXCEPT_AARCH64_IRQ:
      CharCount = AsciiSPrint (Buffer, sizeof (Buffer), "IRQ Exception at 0x%llx\n", LR);
      break;

    case EXCEPT_AARCH64_FIQ:
      CharCount = AsciiSPrint (Buffer, sizeof (Buffer), "FIQ Exception at 0x%llx\n", LR);
      break;

    case EXCEPT_AARCH64_SERROR:
      CharCount = AsciiSPrint (Buffer, sizeof (Buffer), "SError/Abort Exception at 0x%llx\n", LR);
      break;

    default:
      CharCount = AsciiSPrint (Buffer, sizeof (Buffer), "Unknown Exception at 0x%llx\n", LR);
      break;
  }

  // Show Exception Message
  SerialPortWrite ((UINT8 *)Buffer, CharCount);

  // CPU Dead Loop
  CpuDeadLoop ();
}
