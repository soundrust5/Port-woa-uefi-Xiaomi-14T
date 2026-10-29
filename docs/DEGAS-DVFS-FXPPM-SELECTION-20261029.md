# Degas FxPPM performance selection — 2026-10-29

The successful WORK603-fix Windows boot registers PEP and all eight processor
devices but never receives PERF_SET. There is a concrete selection gate in
the actual installed processor driver: zero performance feedback counters
cause its PEP performance initialization to fail even when the PEP advertises
PerformanceStatesSupported=TRUE.

This was a read-only investigation. No provider source, driver package,
firmware or phone state was changed, and no driver was rebuilt.

## Inputs pinned to this Windows image

The captured log is
`evidence/raw/2026-10-29-degas-work603-windows-success/DegasDvfs.log`.
It reports backend_status0, pep_status0, processor_mask255, stable
ppm_notifications13 and perf_set_count0. Qualified thermal ceilings reach the
stock maxima while request words remain at the boot values.

The processor service is FxPPM. The exact captured binary is
`evidence/raw/2026-10-29-degas-work603-windows-success/fxppm.sys`, SHA256
`f89401e1b6025dc1e98e1e634065aa989e3f31677bf41cff28b8140952508991`.
Its matching public PDB was fetched from Microsoft's symbol server using the
binary's RSDS identity `DFF090AFD8DDC1DD2E45CA75ECA11A8A1/fxppm.pdb`.
Disassembly, import identities and public symbols are retained under
`artifacts/degas/dvfs-windows/fxppm*`.

## Exact capability flow

`PepNotifyQueryCapabilities` at FxPPM RVA35E70 issues notification1 through
PoFxProcessorNotification, using the registered core-device handle stored at
its processor context+3F0. It supplies a16-byte local buffer; the first12 bytes
have the familiar PEP capability output layout. The extra byte at offsetC is
initialized separately by FxPPM. Our12-byte output initialization preserves it.

The returned buffer maps as follows:

| PEP output | FxPPM processor-context field | Actual code |
|---|---|---|
| FeedbackCounterCount at buffer+0 | DWORD+3F8 | Copy at35F24..2C, then3630C..24 |
| IdleStateCount at buffer+4 | DWORD+3FC | Same copy path |
| PerformanceStatesSupported at buffer+8 | BYTE+401 | Copy at35F30..34, then36318..1C |
| DiscretePerformanceStateCount at buffer+A | BYTE+400 | Copy at35F40..44, then36328..2C |

`InitPepPerfStates` at RVA34EF8 checks the supported byte at34F48..54. If set,
it immediately loads the feedback count at34F98. Count0 takes the failure path
34F90 with STATUS_UNSUCCESSFUL (C0000001), before notification0B for performance
capabilities or any performance-control registration. The caller at273B4
clears its PEP performance feature bit when this initialization fails.

This explains why successful DPM registration and a full eight-processor mask
do not establish PPM performance support. It also shows that accepting a global
NULL-handle query alone cannot solve the current implementation's zero-counter
gate. Actual per-notification counts/return values would still help distinguish
any additional early capability or feature-mask failures.

## Original-instruction execution

`artifacts/degas/dvfs-windows/fxppm-feedback-gate-evidence.json` retains exact
disassembly and three Unicorn cases executing the original FxPPM instructions:

| PerformanceStatesSupported | FeedbackCounterCount | Result |
|---:|---:|---|
| 1 | 0 | STATUS_UNSUCCESSFUL |
| 1 | 1 | Passes the feedback-count gate |
| 0 | 1 | STATUS_UNSUCCESSFUL |

Only processor-context inputs and the disabled ETW list are modeled. This
bounded execution establishes a necessary condition; it does not establish
that an arbitrary count1 descriptor suffices or perform any hardware access.

The later code35090..351E4 validates descriptors, requires a nonzero nominal
rate and at least one valid frequency-counter descriptor. A performance-only
counter or a fabricated count is not a remedy. The request word remains a
requested target and cannot be relabeled delivered physical frequency.

The next implementation step therefore needs a qualified frequency-feedback
provider, including actual counter semantics, per-CPU affinity, idle treatment,
nominal-rate scaling and counter ownership. Existing A510 AMU-constant-counter
erratum constraints still apply. None of those hardware contracts was added
or claimed by this read-only selection audit.
