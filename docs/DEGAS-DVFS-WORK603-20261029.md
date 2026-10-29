# Degas PEP A0/603: installed-NT work-buffer evidence

Read-only diagnosis; the provider source and installed driver were not changed
or rebuilt during this investigation. No phone action was performed.

The retained record in
`evidence/raw/2026-10-29-degas-pep-a0-fix-v1-result/bugcheck.json` reports
A0, parameter1=603, parameter2=the PEP plugin, parameter3=0 and parameter4=0.
The last recorded PPM notification22 can be concurrent and is not itself
evidence that LPI_SUPPORTED caused the crash.

Microsoft documents WorkInformation as an output pointer to PEP-allocated
storage. Replacing it therefore looked consistent with that public description.
The exact installed NT work route instead consumes the original buffer it
supplied, without reloading the returned pointer. This implementation detail
reproduces the observed error and identifies the provider's incompatible
pointer replacement; it is not a claim about every Windows PEP implementation.
[Microsoft PEP_WORK](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/pep_x/ns-pep_x-_pep_work)

The active NT SHA256 is
`5bb49f8383e38b26ef0c18f3087db3be541e99143b80b92d2f83d824b7e5301c`.
Relevant image-relative instructions are:

- 2CB404..420 clears a WorkInformation buffer at SP+D0, sets the PEP_WORK
  pointer at SP+10 to that buffer and clears NeedWork at SP+18.
- 2CB484..494 invokes the DPM_WORK callback with PEP_WORK at SP+10.
- 2CB4A0 reads only NeedWork. When TRUE, 2CB4B4 explicitly sets dispatcher X1
  to SP+D0 and 2CB4BC calls the work dispatcher at2CD500. There is no reload
  of the callback's substituted pointer at SP+10.
- 2CD530 reads WorkType from that original buffer. Zero dispatches through
  2CD78C; a nonnull plugin takes 2CD79C→2CD77C and reaches A0/603 with the
  plugin and zero work type. The kernel bugcheck wrapper at2CA0F0 supplies A0.

`DegasConstraintWork` fills a separate per-CPU static record with type3 and
replaces WorkInformation with its address. It leaves the kernel's original
zeroed buffer unchanged and sets NeedWork TRUE. The actual NT route therefore
consumes type0, despite the provider's separate record correctly containing3.

Offline Unicorn execution of those original instructions reproduces both
outcomes in `artifacts/degas/dvfs-windows/nt-work603-evidence.json`:

| Modeled callback assignment | Buffer actually consumed | Result |
|---|---|---|
| Replace pointer with a separate valid type3 record | Original zeroed SP+D0 | A0/603, plugin,0,0 |
| Fill supplied SP+D0 with type3 and preserve pointer | Supplied SP+D0 | Reaches valid type3 dispatch |

The callback's field assignments, CFG check, lock/IRQL helpers and stack helper
are modeled. The dispatcher and bugcheck argument assembly are original NT
instructions. The successful case stops before executing an external power
control. This is bounded protocol evidence, not a hardware control test.

The minimal follow-up correction is to fill the supplied WorkInformation
pointee on this NT route, leave its pointer unchanged, and publish NeedWork
only after the record is complete. Keep request context resident until power
control completion; the caller's supplied work buffer is borrowed and must
not be retained or freed by the PEP. An absent supplied buffer must not produce
NeedWork TRUE. Add a host regression using a preinitialized nonnull caller
buffer and the exact installed-NT consumption path before staging that change.

COMPONENT_ACTIVE currently produces NeedWork FALSE and WorkInformation NULL.
The inspected NT notification7 path initializes these outputs to zero and only
requests work when NeedWork becomes TRUE. It therefore does not explain this
nonzero-plugin/type0 work request. Reentrant RequestWorker scheduling and static
record lifetime do not need to fail to obtain the reproduced603: the pointer
replacement alone suffices. They still merit regression coverage when the work
queue is corrected.

## Corrected provider prepared

DegasConstraintWork now preserves the caller-provided WorkInformation pointer,
clears and fills that pointee, and publishes NeedWork only after type3 and all
power-control fields are complete. A null provided buffer reports no work and
does not claim inflight ownership or consume the pending request. The borrowed
buffer is never retained; only the resident CPU completion context is retained.

Actual-handler regressions cover poisoned supplied storage, pointer identity,
PowerControl fields, empty queue, absent storage, inflight skip and completion
followed by retry. They pass alongside the exact-NT603 and605 tests and the full
ARM64 WDK build. Hardware validation remains pending. The no-watchdog RAM logger,
thermal backend and boot-start setting are preserved. The signed update and
source archive are artifacts/degas/dvfs-windows/work603-fix-v1.

## Successful Windows boot after the correction

User confirmed normal Windows boot without BSOD. Android readback matches the
signed work603-fix-v1 driver exactly. A120-sample UFS log spans roughly two
minutes after desktop startup. Backend and PEP registration statuses are0,
all8 devices are registered (mask255), thermal data is qualified throughout,
and all3 ceilings are stock. PPM notification count remains13 and PERF_SET
count remains0 throughout. Controller requests remain1300000/1300000/1200000.
Thus the captured run does not demonstrate active frequency control. It does
demonstrate that the previous fatal work-buffer path no longer blocked boot.

Fresh System.evtx records55 report PerformanceImplementation0 on CPUs0..7.
All8 processor devices bind to the installed Microsoft FxPPM service via
ACPI\\Processor. The next investigation is processor capability selection
and the FxPPM-to-PEP notification contract, rather than changing clock hardware
or bypassing thermal limits. Evidence and exact fxppm.sys/cpu.inf copies are
evidence/raw/2026-10-29-degas-work603-windows-success/.
