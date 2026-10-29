# AP release callsite audit — 2026-10-29

Read-only kernel/firmware audit. No firmware edit, device write, flash, or reboot.
Addresses below are RVAs in the saved `artifacts/degas/cpu-audit/ntoskrnl.exe`,
SHA256 `5bb49f8383e38b26ef0c18f3087db3be541e99143b80b92d2f83d824b7e5301c`.
Public names were resolved using section+offset from `nt-publics.txt` and the
PE section table; objdump's nearest-export labels are frequently misleading.

## The second caller is power resume, not early cold AP initialization

- RVA A8C468 is exactly public symbol `HalpInterruptReinitialize`
  (PAGELK section 10, offset 1128 decimal).
- Its direct caller at A8EF98 belongs to `HalpAcpiPostSleep` (A8EE88).
  A8EF90 tests input flags bit 16 before making this call. A8C548 tests that
  flag again before the processor restart loop.
- A8C584/A8C58C load the HAL processor descriptor array and state array.
  The state-array global at E05380 is exactly `HalpHiberProcState`
  (.data section 24, offset 2118528 decimal).
- A8C574 sets stride 0x6D0. A8C5A0 computes
  `HalpHiberProcState + NtProcessorIndex * 0x6D0`; A8C5A8 passes that saved
  state to `HalStartNextProcessor`. The loop starts at processor index 1.
- A8C5B0 expects HAL result 4. Failure here bugchecks 5C with argument1
  2001, argument2 processor index, argument3 global at C10380, and argument4
  the corresponding 64-byte HAL descriptor. This is not a match for the
  reported 23/4/pointer/D tuple.

This callsite does not provide an alternative cold path that intentionally
skips ready-queue allocation. It restarts processors using previously saved
architectural state after a power transition. For corroboration,
`HalpSaveProcessorState` at 626254 calls `KeSaveStateForHibernate`, then writes
saved caller FP to state+430, caller return address to state+448 (PC),
caller SP to state+440, and return value 1 to state+348. Resume is expected to
continue the power-transition code with its existing processor structures;
it does not synthesize fresh PRCBs or enter fresh `KiSystemStartup`.

## Expected cold AP state and the actual discrepancy

The cold builder at 6CACF8 zeroes the new PCR allocation at 6CAD30 and the
0x6D0 processor state at 6CAD44. It records logical Number at PCR+9A4
(6CAD4C), builds targetPRCB=PCR+980 (6CADDC), synthesizes context
PC=`KiSystemStartup` A91BA0 at 6CAE10..6CAE1C, and publishes
LPB.Prcb=targetPRCB at 6CAE5C. It returns targetPRCB in X0.

`KeStartAllProcessors` takes this return value into X19 at B41E48, passes
that same X19 to `KiAllocateLocalSharedReadyQueue` at B41EB0/B41EB4, checks
status at B41EB8, and releases the AP using the prepared state at B41EF8.
The allocator's successful store is to targetPRCB+8300 at 4FBF48.

**At cold pre-entry, +1908 need not be initialized yet.** The AP later copies
+8300 to +1908 inside `KiInitializeBootStructures` (A937CC/A937DC), before
`KiInitPrcb` consumes it. Therefore the coherent AP1 observation of +1908=0
is not an independent missing allocation. The anomalous observation is
specifically +8300=0, conditional on this being the same runtime kernel and
the intended coherent address/PRCB view.

The reported PC FFFFF80267091BA0 is consistent with KiSystemStartup only
under candidate base FFFFF80266600000. Merely matching the PC's suffix does
not establish runtime image identity or prove this base. LPB.Prcb=TPIDR+980
and Number1 do establish internal consistency of the observed target data;
they do not establish allocator execution or prove the eventual fault.

## Bounded next test

Before changing Windows initialization, establish runtime identity in the
same pre-entry observation: compare a small instruction window at restored
PC against saved A91BA0 bytes; derive a candidate base only after that
match, then validate mapped PE headers/SizeOfImage and its CodeView RSDS
GUID+age, or obtain the loader module record and validate that image.
Capture the processor-state PCR field at +20 alongside LPB.Prcb and TPIDR,
and keep all observations in the same attempt/CPU journal.

Saved-image constants for an isolated identity-reader mode (all extracted
from the actual PE, not inferred from installed version strings):

- First 32 bytes at A91BA0:
  `7f2303d5f353bda9f55b01a9f71300f9ff830bd1fd7b00a9fd030091f40300aa`.
  These instructions contain no relocated absolute pointer.
- DOS e_lfanew=110, PE timestamp=84268839, SizeOfImage=124A000.
- Debug-directory RVA=371F8, size=70 (112 decimal).
- CodeView RVA=4DC78, size=25 (37 decimal); first 24 bytes:
  `52534453c06cd400a1e2483e6ac6c224799cfc2301000000`.
  This is RSDS GUID 00D46CC0-E2A1-3E48-6AC6-C224799CFC23, age1;
  following filename `ntkrnlmp.pdb`.

With four qualified address walks, a targeted mode can record restored
PC instruction bytes, candidate-base headers, candidate-base+4DC78 RSDS,
and the target +8300 value. This is a bounded fingerprint of the saved build;
it should be recorded as failed/unmatched if any walk or comparison fails.
For general loader-driven identity, follow the header's debug directory
instead of assuming these saved-image RVAs. A different runtime build
invalidates the queue-offset/callsite conclusions until it is audited.

If identity matches and +8300 remains zero, a BSP debugger observation at
B41EB4 return and immediately before B41EF8 is the decisive next boundary:
record X19, [X19+8300], LPB.Prcb, state+20 and state+448. This distinguishes
allocation bypass/failure, subsequent overwrite, wrong target state, and
an AP visibility/translation issue. It also directly identifies the caller
instead of inferring it from AP data. No such trace has yet been obtained;
no concrete firmware correction follows from this audit alone.
