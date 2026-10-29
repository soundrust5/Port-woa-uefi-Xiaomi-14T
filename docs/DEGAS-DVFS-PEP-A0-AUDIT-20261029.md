# Degas PEP INTERNAL_POWER_ERROR contract audit — 2026-10-29

The first active-provider boot showed bugcheck0xA0. Its parameters have not yet
been recovered, so the particular crash cannot be attributed conclusively.
This audit nevertheless found and repaired a concrete fatal callback-contract
defect that exists independently of the hardware DVFS path.

The PEP DPM callback's Boolean return reports whether the notification was
handled. DeviceAccepted separately reports ownership. The old provider returned
FALSE for unknown PREPARE/REGISTER/ABANDON identities, confusing the two results.
It could therefore crash Windows before accepting even one processor.

## Exact installed NT evidence

The tested NT is the active capture at
`evidence/raw/2026-10-29-degas-active-boot-identity/ntoskrnl.exe`, SHA256
`5bb49f8383e38b26ef0c18f3087db3be541e99143b80b92d2f83d824b7e5301c`.
The host test executes its original ARM64 instructions with Unicorn, starting
at the callback-return check. Callback handled and ownership fields are inputs;
no callback or kernel function executes against a phone.

| Check | FALSE handled result | TRUE handled result with ownership0 |
|---|---|---|
| PREPARE return at RVA2d0034 | Reaches actual KeBugCheckEx export with codeA0, parameter1=605, parameter2=1 | Continues normally with declined ownership |
| REGISTER return at RVA2d0148 | Reaches actual KeBugCheckEx export with codeA0, parameter1=605, parameter2=3 | Continues normally with declined ownership |

The original NT bugcheck wrapper at RVA2ca0f0 moves the incoming violation and
notification arguments into their final positions, sets bugcheck codeA0, and
calls KeBugCheckEx at RVA25d7c0. The emulator stops at that export after retaining
all parameters. All four cases pass; exact disassembly and identities are in
`artifacts/degas/dvfs-windows/nt-dpm-handled-test.json`.

Microsoft describes subcode605 as failure to acknowledge mandatory DPM;
617 concerns an invalid acceptance enum, and705 concerns mutation of input
fields. Those remain different diagnoses pending actual crash parameters.
[Microsoft bugcheckA0 reference](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/bug-check-0xa0--internal-power-error)

## Minimal corrected ownership contract

PREPARE, REGISTER and ABANDON now acknowledge handled declines with TRUE and
DeviceAccepted FALSE/NotAccepted. Unknown or malformed device IDs and duplicate
registrations acquire no ownership and cannot replace an existing processor
handle. The accepted value remains the actual WDK `PepDeviceAccepted` enum2;
the legacy reserved enum slot explains why it is not1 in pep_x.h. No evidence
identified that enum as invalid for this kernel.
[Microsoft registration structure](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/pepfx/ns-pepfx-_pep_register_device_v2)

DEVICE_POWER_STATE is acknowledged without modifying any bytes: every field
is input. There are no additional external power resources to transition.
Native PSCI remains responsible for its existing CPU power route. Framework
component-active notification requests no deferred resource work; component
Fx preparation marks only its Completed output TRUE. This does not implement
a CPU idle state or add a processor-idle executor.
[Device power fields](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/pepfx/ns-pepfx-_pep_device_power_state),
[component preparation output](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/pepfx/ns-pepfx-_pep_notify_component_idle_state)

Performance-only PPM support is allowed: Microsoft explicitly permits
IdleStateCount=0 without implementing the other PPM idle notifications.
That does not remove DPM acknowledgment responsibilities after accepting a
device. Unsupported incoming power controls are handled with Status=NOT_SUPPORTED
and BytesReturned=0; input buffers and control fields remain unchanged.
[Microsoft PPM capabilities](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/pep_x/ns-pep_x-_pep_ppm_query_capabilities)

## Verification and next evidence

`test_provider_handler.py` extracts the current actual DPM handler body and its
real path/handle lookup helpers, compiles them with isolated host definitions
and actual protocol code, and exercises unknown, malformed, null-ID, duplicate
and accepted CPU registrations. It verifies byte-preservation for Dx inputs
and the component preparation output. Kernel helpers are modeled; the handler
body is not rewritten or reimplemented in the test. The result retains its
SHA256 in `artifacts/degas/dvfs-windows/provider-handler-test.json`.

`test_nt_dpm_handled.py` supplies the four exact-NT cases above. Both tests and
the full ARM64 WDK build pass. These checks do not establish that the first
phone crash had subcode605, or that every runtime Windows callback now works.
Recovering full bugcheck parameters and the last callback/identity before the
crash remains necessary before attributing the observed failure.

Retained-RAM logging must run before user-service startup. The ordinary
KeRegisterBugCheckCallback callback is documented to run when the OS bugchecks;
dump-reason callbacks serve separate dump-processing purposes. Its storage and
code must remain resident, with no lock acquisition, allocation, file I/O or
new mapping in the callback. Registration success and logger initialization
should be retained separately from the bugcheck payload.
[Microsoft ordinary bugcheck callback](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-keregisterbugcheckcallback)

No phone action, driver disable, boot image change or firmware change was
performed by this audit subtask.

## Hardware retry prepared

Installed the corrected provider and ordinary bugcheck RAM callback without
changing the boot-start service setting. Only CrashControl.AutoReboot and
DisplayParameters were set to1 in a fresh clean SYSTEM hive. Host and phone
backups precede replacement; readback SHA256 and GPT checks pass. Evidence:
`artifacts/degas/dvfs-windows/a0-fix-v1/staged.json`.

The callback is independent of SCM, UFS logging and a disk crash dump. It writes
five words (code+four parameters) into the existing reserved ramoops slot30 at
0x480ae000, preserving the Android routing word at+0xf00. Data validity is
qualified against the exact installed NT export layout (KiBugCheckData minus
KeBugCheckEx =0xb5e1e0); failure is recorded and never bypassed with a scan.
The slot uses a versioned body, CRC32+inverse and outer header committed last.
Only after RAM commit does the callback arm the existing watchdog ABI for a
10-second warm reset. Callback execution, export resolution and reset behavior
still require actual phone observation; hard reset retention is not promised.

Actual callback host tests pass under ASan/UBSan: exact code/parameter bytes,
CRC, no allocation/locks/system calls during callback, preservation of the
second2KiB, blocking of later normal markers and missing-export rejection.
The Python decoder rejects corrupted CRC. Tests do not establish hardware
retention or callback invocation. UEFI now recognizes DGBUG001 and copies the
previous entire slot to its independent RAM console before later OS startup.
The diagnostic viewport is inset so parameters remain readable at rounded edges.

Native8-CPU bringup is unchanged. New qualified image:
`artifacts/degas-pep-a0-diagnostic-v1/boot.img`, SHA256
`6ec70176a46c35a3878ca644c16cca7f898041ef0c91d129c93500fe370140c5`.
Windows retry awaits the user's boot-menu selection. The original observed A0
subcode remains unknown and should not yet be called605.
