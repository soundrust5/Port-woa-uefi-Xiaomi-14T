/**
  Copyright (c) 2011-2017, ARM Limited. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/PcdLib.h>
#include <Library/DebugLib.h>
#include <Library/PrePiLib.h>
#include <Library/DebugAgentLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/SerialPortLib.h>
#include <Library/PlatformSecLib.h>
#include <Library/PrePiHobListPointerLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/MemoryMapHelperLib.h>
#include <Library/TimerLib.h>
#include <Library/HobLib.h>

#include "Sec.h"
#if defined (ONEPLUS12_SEC_PROBE) && ONEPLUS12_SEC_PROBE >= 6
#include <Op12Probe.h>
#endif

#if defined (ONEPLUS12_SEC_PROBE) && ONEPLUS12_SEC_PROBE >= 2
// Set by the guarded assembly handoff, before any platform initialization.
UINTN Op12ProbeContext[3] = { 0, 0, 0 };

VOID
Op12FinishSecProbe (UINTN Stage, UINTN StackBase, UINTN StackSize, UINTN ExceptionType, UINTN ExceptionLr)
{
  volatile UINT64 *Record = (volatile UINT64 *)Op12ProbeContext[2];
  UINT64 Value;
  Record[0] = 0x3143525043554DULL; // "MUCPRC1" byte marker
  Record[1] = Stage;
  Record[3] = StackBase;
  Record[4] = StackSize;
  __asm__ volatile ("mov %0, sp" : "=r" (Value)); Record[5] = Value;
  __asm__ volatile ("mrs %0, sctlr_el1" : "=r" (Value)); Record[6] = Value;
  __asm__ volatile ("mrs %0, vbar_el1" : "=r" (Value)); Record[7] = Value;
  __asm__ volatile ("mrs %0, cpacr_el1" : "=r" (Value)); Record[8] = Value;
  __asm__ volatile ("mrs %0, daif" : "=r" (Value)); Record[9] = Value;
  Record[10] = ExceptionType;
  __asm__ volatile ("mrs %0, esr_el1" : "=r" (Value)); Record[11] = Value;
  Record[12] = ExceptionLr;
  __asm__ volatile ("mrs %0, far_el1" : "=r" (Value)); Record[13] = Value;
  Record[14] = (UINTN)SecVectorTable;
  Record[15] = (UINTN)Op12FinishSecProbe;
#if OP12_UFS_DIAGNOSTIC >= 2
  // A synchronous abort must not bypass UFS DMA teardown and free its buffers.
  // Only the UFS owner clears this flag after quiescing/restoring the queue.
  if (Record[31] & 0x1F00) {
    WriteBackDataCacheRange ((VOID *)Record, 512);
    CpuDeadLoop ();
  }
#endif
#if ONEPLUS12_SEC_PROBE >= 3
  __asm__ volatile ("mrs %0, tcr_el1" : "=r" (Value)); Record[20] = Value;
  __asm__ volatile ("mrs %0, ttbr0_el1" : "=r" (Value)); Record[21] = Value;
  __asm__ volatile ("mrs %0, mair_el1" : "=r" (Value)); Record[22] = Value;
  __asm__ volatile ("mrs %0, tpidr_el0" : "=r" (Value)); Record[23] = Value;
  extern VOID Op12MmuReturn (UINTN Callback, UINTN TailBase, UINTN TailSize);
  Op12MmuReturn (Op12ProbeContext[1], Record[18], Record[19]);
#endif
  __asm__ volatile ("dsb sy" ::: "memory");
  ((VOID (*)(VOID))Op12ProbeContext[1]) ();
  CpuDeadLoop ();
}
#endif

#ifndef MDEPKG_NDEBUG
STATIC
VOID
PrintFirmwareVersion ()
{
  // Display memory is owned by the display library, not cleared as a carveout.

  // Print UEFI Version Message
  DEBUG ((EFI_D_WARN, "\n"));
  DEBUG ((EFI_D_WARN, "Project Silicium for %a %a\n",            FixedPcdGetPtr (PcdSmbiosSystemManufacturer), FixedPcdGetPtr (PcdSmbiosSystemModel)));
  DEBUG ((EFI_D_WARN, "Firmware Version %s Build at %a on %a\n", PcdGetPtr (PcdFirmwareVersionString), __TIME__, __DATE__));
  DEBUG ((EFI_D_WARN, "\n"));
}
#endif

STATIC
EFI_STATUS
InitializeMemory (
  IN UINTN StackBase,
  IN UINTN StackSize)
{
  EFI_STATUS                    Status;
#if !defined (ONEPLUS12_SEC_PROBE) || ONEPLUS12_SEC_PROBE < 3
  EFI_MEMORY_REGION_DESCRIPTOR  UefiMemoryRegion;
#endif
  EFI_HOB_HANDOFF_INFO_TABLE   *HobList;

#if defined (ONEPLUS12_SEC_PROBE) && ONEPLUS12_SEC_PROBE >= 3
  // The diagnostic wrapper owns this bounded heap inside its reserved tail.
  volatile UINT64 *Record = (volatile UINT64 *)Op12ProbeContext[2];
  UINTN UefiMemoryBase = Record[16];
  UINTN UefiMemorySize = Record[17];
#ifndef OP12_WINDOWS_BOOT
#define OP12_WINDOWS_BOOT 0
#endif
  if (UefiMemorySize != (ONEPLUS12_SEC_PROBE >= 6 ? (OP12_WINDOWS_BOOT ? 0x4000000 : 0x1000000) : 0x200000) || (UefiMemoryBase & 0xFFF) != 0 ||
      UefiMemoryBase < Record[18] || Record[19] < UefiMemorySize ||
      UefiMemoryBase - Record[18] > Record[19] - UefiMemorySize) {
    return EFI_INVALID_PARAMETER;
  }
#else
  // Locate "DXE Heap" Memory Region
  LocateMemoryRegionByName ("DXE Heap", &UefiMemoryRegion);
  LocateMemoryRegionByName ("DXE_Heap", &UefiMemoryRegion);

  // Save UEFI Memory Details
  UINTN UefiMemoryBase = UefiMemoryRegion.Address;
  UINTN UefiMemorySize = UefiMemoryRegion.Length;

  // Verify UEFI Memory Base
  if (UefiMemoryBase == 0 && UefiMemorySize == 0) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate \"DXE Heap\" Memory Region!\n"));
    return EFI_NOT_FOUND;
  }

#endif
#if defined (ONEPLUS12_SEC_PROBE) && ONEPLUS12_SEC_PROBE >= 3
  // Entry C=0 alone does not establish absence of stale cache lines.
  InvalidateDataCacheRange ((VOID *)(UINTN)Record[18], (UINTN)Record[19]);
#if ONEPLUS12_SEC_PROBE >= 6
  // Discard stale inherited framebuffer lines before enabling our cache map.
  InvalidateDataCacheRange ((VOID *)(UINTN)0xD5100000, 0x1167000);
#endif
#endif
  // Initialize HOB List
  HobList = HobConstructor ((VOID *)UefiMemoryBase, UefiMemorySize, (VOID *)UefiMemoryBase, (VOID *)(UefiMemoryBase + UefiMemorySize));
  PrePeiSetHobList (HobList);

  // Initialize MMU & Memory HOBs
  Status = MemoryPeim (UefiMemoryBase, UefiMemorySize);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Build Stack HOB
  BuildStackHob (StackBase, StackSize);

  return EFI_SUCCESS;
}

STATIC
VOID
SecMain (
  IN UINTN StackBase,
  IN UINTN StackSize)
{
  EFI_STATUS Status;

#ifndef MDEPKG_NDEBUG
  // Initialize Serial Port
  Status = SerialPortInitialize ();
  if (EFI_ERROR (Status)) {
    return;
  }

  // Print Firmware Version
  PrintFirmwareVersion ();

  // Initialize Debug Agent
  InitializeDebugAgent (DEBUG_AGENT_INIT_POSTMEM_SEC, NULL, NULL);
  SaveAndSetDebugTimerInterrupt (TRUE);
#endif

  // Initialize Memory
  Status = InitializeMemory (StackBase, StackSize);
  if (EFI_ERROR (Status)) {
    return;
  }

  // Build CPU HOB
  BuildCpuHob (ArmGetPhysicalAddressBits (), PcdGet8 (PcdPrePiCpuIoSize));

  // Set the Boot Mode
  SetBootMode (BOOT_WITH_DEFAULT_SETTINGS);

  // Initialize Platform HOBs
  Status = PlatformPeim ();
  if (EFI_ERROR (Status)) {
    return;
  }

  // Run Library Cconstructors
  ProcessLibraryConstructorList ();

  // Decompress all FVs
  Status = DecompressFirstFv ();
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Decompress FV! Status = %r\n", Status));
    return;
  }

  // Load DXE Core
  Status = LoadDxeCoreFromFv (NULL, 0);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Load DXE Core! Status = %r\n", Status));
  }
}

#if defined (ONEPLUS12_SEC_PROBE) && ONEPLUS12_SEC_PROBE >= 6
STATIC VOID
Op12EnterDxeProbe (UINTN StackBase, UINTN StackSize)
{
  STATIC CONST EFI_GUID ProbeGuid = OP12_PROBE_HOB_GUID;
  volatile UINT64 *Record = (volatile UINT64 *)Op12ProbeContext[2];
  OP12_PROBE_HANDOFF Handoff = {
    OP12_PROBE_HANDOFF_MAGIC, Record, Op12FinishSecProbe, StackBase, StackSize
  };
  EFI_STATUS Status;
  Record[29] = 0x600;
  BuildCpuHob (ArmGetPhysicalAddressBits (), PcdGet8 (PcdPrePiCpuIoSize));
  SetBootMode (BOOT_WITH_DEFAULT_SETTINGS);
  if (BuildGuidDataHob (&ProbeGuid, &Handoff, sizeof Handoff) == NULL) {
    Op12FinishSecProbe (0xEF, StackBase, StackSize, 0, EFI_OUT_OF_RESOURCES);
  }
  // Waffle stage 6 overrides this with PlatformPeiLibNull: FV HOB only.
  Status = PlatformPeim ();
  if (!EFI_ERROR (Status)) {
    Record[29] = 0x601;
    ProcessLibraryConstructorList ();
    Record[29] = 0x602;
    Status = DecompressFirstFv ();
  }
  if (!EFI_ERROR (Status)) {
    Record[29] = 0x603;
    Status = LoadDxeCoreFromFv (NULL, 0);
  }
  Record[30] = Status;
  // The dedicated DXE probe returns via Finish; DXE Core must not return here.
  Op12FinishSecProbe (0xEF, StackBase, StackSize, 0, Status);
}
#endif

VOID
SecEntry (
  IN UINTN StackBase,
  IN UINTN StackSize)
{
  // Disable Data Cache
  ArmDisableDataCache ();

  // Invalidate Instruction Cache
  ArmInvalidateInstructionCache ();

  // Enable Instruction Cache
  ArmEnableInstructionCache ();

  // Invalidate Stack & UEFI FD D-Cache
  InvalidateDataCacheRange ((VOID *)StackBase, StackSize);
  InvalidateDataCacheRange ((VOID *)FixedPcdGet64 (PcdFdBaseAddress), FixedPcdGet32 (PcdFdSize));

  // Verify Exception Vector Table
  ASSERT (((UINTN)SecVectorTable & ARM_VECTOR_TABLE_ALIGNMENT) == 0);

  // Enable new Exception Vector Table
  ArmWriteVBar ((UINTN)SecVectorTable);

  // Do Platform Specific Initialization
  PlatformInitialize ();

#if defined (ONEPLUS12_SEC_PROBE) && ONEPLUS12_SEC_PROBE >= 2
  // C entry, stack, VFP setup, cache maintenance and VBAR are now exercised.
  // Stop before HOB construction / MemoryPeim can enable translation.
#if ONEPLUS12_SEC_PROBE >= 3
  EFI_STATUS ProbeStatus = InitializeMemory (StackBase, StackSize);
#if ONEPLUS12_SEC_PROBE >= 4
  volatile UINT64 *ProbeRecord = (volatile UINT64 *)Op12ProbeContext[2];
  UINTN MmioBase = ProbeRecord[24];
  if (!EFI_ERROR (ProbeStatus) && ProbeRecord[25] == 2048 &&
      MmioBase >= ProbeRecord[18] && ProbeRecord[19] >= 2048 &&
      MmioBase - ProbeRecord[18] <= ProbeRecord[19] - 2048) {
    volatile UINT64 *MmioRecord = (volatile UINT64 *)MmioBase;
    STATIC CONST UINT32 Offsets[] = {0x0, 0x5000, 0x500c, 0x5014, 0x5024, 0x5030, 0x5034, 0x5038, 0x7000, 0x700c, 0x7014, 0x7024, 0x7030, 0x7034, 0x7038, 0x9000, 0x900c, 0x9014, 0x9024, 0x9030, 0x9034, 0x9038, 0xb000, 0xb00c, 0xb014, 0xb024, 0xb030, 0xb034, 0xb038, 0x25000, 0x2500c, 0x25014, 0x25024, 0x25030, 0x25034, 0x25038, 0x27000, 0x2700c, 0x27014, 0x27024, 0x27030, 0x27034, 0x27038, 0x29000, 0x2900c, 0x29014, 0x29024, 0x29030, 0x29034, 0x29038, 0x2b000, 0x2b00c, 0x2b014, 0x2b024, 0x2b030, 0x2b034, 0x2b038, 0x2d000, 0x2d00c, 0x2d014, 0x2d024, 0x2d030, 0x2d034, 0x2d038, 0x2f000, 0x2f00c, 0x2f014, 0x2f024, 0x2f030, 0x2f034, 0x2f038, 0x16014, 0x1601c, 0x160f4, 0x17014, 0x1701c, 0x170f4, 0x18014, 0x1801c, 0x180f4, 0x19014, 0x1901c, 0x190f4, 0x1a014, 0x1a01c, 0x1a0f4, 0x1b014, 0x1b01c, 0x1b0f4};
    for (UINTN i = 0; i < ARRAY_SIZE (Offsets); i++) {
      MmioRecord[i * 2] = 0x0AE00000 + Offsets[i];
      ProbeRecord[26] = i; // First uncompleted read if an exception occurs.
      MmioRecord[i * 2 + 1] = *(volatile UINT32 *)(UINTN)(0x0AE00000 + Offsets[i]);
      ProbeRecord[26] = i + 1;
    }
  } else if (!EFI_ERROR (ProbeStatus)) {
    ProbeStatus = EFI_INVALID_PARAMETER;
  }
#if ONEPLUS12_SEC_PROBE >= 5
  UINT64 CounterValue;
  __asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (CounterValue)); ProbeRecord[27] = CounterValue;
  __asm__ volatile ("isb; mrs %0, cntpct_el0" : "=r" (CounterValue)); ProbeRecord[28] = CounterValue;
#if ONEPLUS12_SEC_PROBE >= 6
  if (!EFI_ERROR (ProbeStatus)) {
    Op12EnterDxeProbe (StackBase, StackSize);
  }
#endif
  Op12FinishSecProbe (EFI_ERROR (ProbeStatus) ? 0xEF : 5,
#else
  Op12FinishSecProbe (EFI_ERROR (ProbeStatus) ? 0xEF : 4,
#endif
#else
  Op12FinishSecProbe (EFI_ERROR (ProbeStatus) ? 0xEF : 3,
#endif
                     StackBase, StackSize, 0, ProbeStatus);
#else
  Op12FinishSecProbe (2, StackBase, StackSize, 0, 0);
#endif
#endif

  // Enter SEC Main Function
  SecMain (StackBase, StackSize);

  // Something went Wrong
  CpuDeadLoop ();
}
