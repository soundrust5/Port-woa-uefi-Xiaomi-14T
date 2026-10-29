# Windows ARM64 PRCB initialization audit — 2026-10-28

Read-only audit; no Windows or firmware source modifications.

## Inputs and limitations

- Kernel: `artifacts/degas/cpu-audit/ntoskrnl.exe`, SHA256 `5bb49f8383e38b26ef0c18f3087db3be541e99143b80b92d2f83d824b7e5301c`.
- Public PDB downloaded from Microsoft symbol server: `https://msdl.microsoft.com/download/symbols/ntkrnlmp.pdb/00D46CC0E2A13E486AC6C224799CFC231/ntkrnlmp.pdb`, SHA256 `3a2346d8beb860b74978805c7950ecd0c03f8ce2da32f8df8395835ba09aa090`.
- GUID matches PE: `00D46CC0-E2A1-3E48-6AC6-C224799CFC23`. PE CodeView age1; PDB Info stream age6; raw DBI stream age1. **This satisfies Microsoft's matching rule**: Info age >= image age, DBI age == image age. Primary implementation: https://github.com/microsoft/microsoft-pdb/blob/master/PDB/dbi/pdb.cpp , comment beginning "For an acceptable match". Initial concern from llvm-pdbutil summary age alone is therefore resolved; public symbol names are usable for this PE.
- User fault PC `FFFFF801794FD958`, exception `C0000005`, write (`1`) to VA0. Runtime kernel base has NOT been captured.

## Candidate chain

If nt runtime base was `FFFFF80179000000`, fault RVA is `4FD958`, exactly the first instruction of the public-symbol candidate `KiInitializeSharedReadyQueue`: `stp xzr,xzr,[x0]`. NULL X0 exactly reproduces the reported exception parameters. Other kernel bases or another module have not been excluded.

1. `KiInitializeBootStructures` candidate RVA `A936E0` derives PRCB from X18+980. It reads logical processor number from PCR+9A4.
2. BSP path (processor number0) explicitly initializes PRCB+8300 to a static queue address at RVA `A937BC`.
3. Secondary path branches at `A9372C` to `A937C4`, skipping that static initialization. Both paths load `[PRCB+8300]` at `A937CC` and store it to `[PRCB+1908]` at `A937DC`.
4. Later calls `KiInitPrcb` candidate RVA `A92C10`; at `A92C68` loads X0=[PRCB+1908], X1=PRCB, calls RVA4FD958 at `A92C70`.

Thus, IF fault mapping is correct, the failure is consistent with a secondary whose expected local ready-queue pointer was absent/stale/wrong. It is not directly proof of a zero PSCI context in this Parking boot.

## Queue allocation before AP release

`KiAllocateLocalSharedReadyQueue` candidate RVA `4FBEE0` calls ExAllocatePool3 at `4FBF34`, checks NULL, returns failure if allocation fails, and stores successful allocation in PRCB+8300 at `4FBF48`.

Only direct call found is in `KeStartAllProcessors` candidate RVA `B41C50`:

- PRCB pointer X19 is prepared through several checked initialization stages.
- `B41EB0`: X0=X19.
- `B41EB4`: calls allocator RVA4FBEE0.
- `B41EB8`: tests returned NTSTATUS sign bit, branches to failure path on failure.
- Additional checked allocations run.
- `B41EF8`: calls `HalStartNextProcessor` RVA484C10.

The normal kernel-controlled AP-start sequence therefore allocates the queue before requesting release. No local topology branch in this sequence was found that releases AP while deliberately skipping queue allocation. Do NOT patch out the NULL dereference in Windows: it is evidence of a missing prerequisite, not the prerequisite itself.

## Useful next diagnostic

Record exact CPU-ID value and physical mailbox/entry handoff observed by every AP, with per-CPU private progress markers. That can establish whether Windows releases the intended AP once and which mailbox/context it sees. Check freshness and coherency rather than infer from readiness before Windows. Runtime kernel base requires live module/loader mapping (KD or a targeted LOADER_PARAMETER_BLOCK observation); static subtraction of preferred ImageBase is invalid with ASLR. UEFI loaded-image handles are not guaranteed to include ntoskrnl, which winload loads without UEFI StartImage.

Current evidence supports investigating wrong/stale per-CPU startup state. It does not independently identify which CPU crashed or prove that the fault PC is in this kernel.

## Follow-up: exact CPU_ID written by this Windows GICv3 implementation

The GICC parser (RVA48A8D8 onward) has distinct GICv3 and legacy branches:

- `48A938`: loads MPIDR from GICC+0x44.
- `48A93C`: X9 = MPIDR >> 8.
- `48A940`: W8 = W9 XOR W8.
- `48A944`: W8 &= 0x00ffffff.
- `48A948`: W8 ^= W9.
- `48A94C`: writes W8 to first DWORD of the 64-byte HAL processor descriptor.
- Only the legacy branch at `48A958` loads GICC CPU Interface Number at+4 instead.

The resulting processor ID for the actual declared MPIDRs 0x100..0x700 is 0x100..0x700, not0. HalpInterruptSetProcessorStartContext later writes this descriptor first DWORD to the parking mailbox CPU_ID (RVA47F8A0, independently identified by parking-contract agent).

Therefore an AP parking loop that only accepts CPU_ID==0 rejects every legitimate secondary release under this Windows implementation. Expected ID for this hardware is I<<8 (or the general compressed-MPIDR formula), validated per private mailbox/CPU. This is a concrete correction to the strict-zero diagnostic, not proof it fixes the original KMODE: original v1 accepted any non-FFFFFFFF value and would have accepted these IDs already.

The new user-reported 5C parameters (23,4,FFFFF7B840001860,D) have not been precisely matched to a saved-kernel bugcheck callsite. A literal5C/subtype23 path was not found. Do not claim a precise HAL subreason solely from these typed values. Strict-zero rejection independently explains why no AP release markers appeared.

## v6 PRCB probe interpretation (2026-10-28)

Reported all reader statuses0, processor number0, local/shared queue resembling FFFFF800B7E15500, with restoredPCFFFFF800B7A91BA0. For candidateNTbaseFFFFF800B7000000 the static BSP shared queue is exactly RVA E15500, so these values match the BSP's initialized structures.

However this does NOT yet prove that actual Windows startup reads the same values: the diagnostic physically reads LPB/PRCB before enabling the Windows MMU, while the BSP may have newer dirty Normal-WB cache lines. Physical uncached/device reads can differ from later coherent VA reads. Compare the captured same-boot initialTPIDR (mailbox+40) against derivedPCR=LPB.PRCB-980 before claiming a wrong context. Even an observed mismatch must account for stale physical memory visibility.

Normal AP preparation RVA6CACF8 loads globalLPB intoX25, initializes allocatedPCR/PRCB, records logicalnumber, buildsKPROCESSOR_STATE withCONTEXT.PC=KiSystemStartup, then at6CAE5C stores targetPRCB to LPB+88 (paired with stack toLPB+80). KeStartAllProcessors then allocates localQ and only afterwards callsHalStartNextProcessor.

Therefore current evidence cannot justify modifying Windows queue initialization, overriding LPB+88, or flushing live Windows caches blindly. The first real AP architectural read under Windows mappings is more decisive than the diagnostic physical PoC view.
