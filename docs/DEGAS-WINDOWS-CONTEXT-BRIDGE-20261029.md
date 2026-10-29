# Offline native Windows AP context bridge — 2026-10-29

The smallest concrete Windows-side experiment found in this audit is a
56-byte replacement of the native AP stub's initial instructions. It recovers
the existing Windows mailbox physical address from the executing CPU's MPIDR
and a UEFI-owned contiguous allocation. It retains native PSCI CPU_ON and the
rest of the Windows AP startup sequence. Host instruction tests pass; Windows
SMP and integrity acceptance remain unproved. No phone operation, original
Windows binary edit, or GZ/TEE/LK/preloader modification was performed.

## Exact inputs and identities

The read-only active-boot capture is
`evidence/raw/2026-10-29-degas-active-boot-identity/manifest.json`.

| Saved active file | SHA256 |
|---|---|
| ntoskrnl.exe | 5bb49f8383e38b26ef0c18f3087db3be541e99143b80b92d2f83d824b7e5301c |
| winload.efi | cce44a3beddbfb9a0b980522ab6b442d99507b1880e020ad637306921dbb5c7e |
| bootmgfw.efi | 3dca6d137e7b61e231969d693d9fb86e590dea066931e686d40221f72d74ac8e |

The kernel's SizeOfImage is 0x124A000 and preferred ImageBase is 0x140000000.
Its RSDS GUID is 00D46CC0-E2A1-3E48-6AC6-C224799CFC23, age1,
name ntkrnlmp.pdb. The installed BCD capture selects this ordinary kernel,
with no kernel or HAL override. These exact file bytes also match the
existing `artifacts/degas/cpu-audit/ntoskrnl.exe` audit reference. Public-symbol
matching qualifications are recorded in DEGAS-WINDOWS-PRCB-AUDIT-20261028.md.

## Native call and copied-code contract

The pinned kernel contains this exact cold-start sequence:

| RVA | Operation |
|---|---|
| 47F8C8 | HalpInterruptStartProcessorPsci entry |
| 47F8F8 | X2 = target descriptor+18, mailbox physical address |
| 47F8FC | X0 = descriptor+30, target affinity |
| 49834C | HalpPowerPsciCpuOn moves mailbox X2 into X3 |
| 49835C | X2 = [X1], common physical startup entry |
| 4A8DD8 | PsciCpuOn loads function ID C4000003 |
| 4A8DF0 | X1 = constructed MPIDR; X2/X3 remain entry/context |
| 498440 | HalpPowerPsciSmcHandler, selected native dispatch |
| 6261D8 | actual SMC #0 |

The host test executes that entire selected instruction path with the CFG
validator and external SMC modeled. CPUs1,4,7 reach the SMC with their distinct
MPIDR, common entry and full mailbox context. The context is already correct
inside Windows. Intercepting an EFI protocol cannot repair the later direct
SMC. The separate GZ audit explains why the AP subsequently receives X0=0.

Native parsing accepts a nonzero GICC.ParkedAddress as the target mailbox even
while FADT declares PSCI. The parsing helper at48A490 stores its supplied
physical address into descriptor+18 and its mapping into descriptor+20.
This does not require advertising legacy parking or prestarting APs.

Before CPU_ON, native context preparation at47F830 copies the prepared0x6D0
bytes into mailboxVA+20, stores the corresponding VA into mailbox+10, stores
the common physical stub entry into mailbox+8, executes DSB SY, and publishes
the processor ID. Windows therefore remains responsible for filling its
actual per-CPU state. The bridge only reconstructs the physical mailbox
pointer that GZ loses.

The common-page builder at498170 zeroes page+10..FFF and copies exactly0x180
bytes from native template2000A0..200220 into page+10. It fixes the copied
body target at page+20 to pagePA+30, and the copied virtual continuation at
page+28 to the mappedpage+130. The thunk at page+10 jumps through the former.
The original body uses X0 for the first time at2000DC:
`ADD X2,X0,#20`. With zero context, the later original200100
`LDR X4,[X2,#C0]` reads physical0xE0.

The host test also executes the actual builder498170..49825C, with its five
external memory helpers modeled, and verifies every output page byte and both
fixups. The copied layout used for the AP tests exactly matches that output.

## Minimal prefix experiment

UEFI would reserve eight contiguous4KiB pages below4GiB, base B, and publish
GICC.ParkedAddress=B+(Aff1<<12) for the known affinities0..7. CPU0's unused
slot is retained to keep the physical calculation direct. These are owned
reserved pages, not a guessed free address. Existing Windows mailbox state
occupies those pages; no extra save of Windows X3 is needed because its value
is constrained to that published address and is recoverable independently.

Replace only source RVAs2000C0..2000F8, preserving every later instruction:

```asm
mrs  x1, daif
tbz  w1, #7, .                 // Same infinite failure if DAIF.I was clear
msr  daifset, #1
mrs  x0, mpidr_el1
ubfx x0, x0, #8, #3            // Degas Aff1, ONLY after full topology validation
movz x2, #(B & 0xffff)
movk x2, #((B >> 16) & 0xffff), lsl #16
add  x0, x2, x0, lsl #12       // Restored original Windows mailbox physical X0
add  x2, x0, #0x20
adrp x1, .                    // Common startup page, even after copying
ldr  x4, [x1, #8]
add  x5, x4, #0x800
msr  ttbr1_el1, x5
isb
```

The original prefix has a separate TBNZ and infinite B loop, three
instructions to compute the page base, and a zero/MOV/OR sequence before the
TTBR addition. Folding those equivalent operations makes room for MPIDR
recovery without enlarging the template, changing its thunk/literals, adding
a code cave, changing the CPU_ON callsite, or replacing a HAL status.
At2000F8 all relevant live inputs match the original successful nonzero
context path: X0, X1, X2, X4, X5 and TTBR1. The original instructions then
install the remaining context, stack, mappings, and virtual continuation.

The three-bit extraction is valid only for the exact validated Degas MPIDR
affinity map. It must never be used as a generic MPIDR hash: other Aff0,
Aff2, Aff3, or Aff1 values could alias a wrong mailbox. The firmware must abort
this experiment on an incompatible map. The mechanism repairs this cold AP
stub only; GZ's contract for other CPU_ON consumers and suspend/resume remains
unchanged and unproved.

## Interception and lifetime boundary

The original source template resides in section8 `.text`, RVA200000,
VirtualSize46A794, raw offsetFF600, flags68000020. It is persistent executable
kernel text. It is not discardable INIT. Full template SHA256 after loading
normalization is
24f05c67ffdf1b8b4c714dd1c71009de210f8432670a6743c0524567b89664ef.
The sole DIR64 relocation inside the template is at200218: the raw pointer
1402001FC becomes actualNTbase+2001FC. A loaded-image guard must validate that
value and normalize it before comparison. The56-byte modified prefix has no
base relocations.

The actualNTbase in that DIR64 expression is the Windows relocation virtual
base. It is distinct from the physical backing address scanned at EBS. A
physical candidate at50000000 can contain the targetFFFFF800D3E001FC, meaning
virtualbaseFFFFF800D3C00000. A locator may infer that numeric virtualbase from
the exact guarded target, require high canonical/page-aligned form, and then
normalize the template; it must never dereference that VA through UEFI.
It must not require target=physicalbase+2001FC or accept the unrelocated raw
preferred pointer as evidence of a live Windows kernel. The helper
`validate_physical_candidate()` models these separate physical/virtual values.

UEFI LoadImage/StartImage is not a sufficient kernel interception point.
Bootmgfw and winload have their own boot application/image loading paths;
ntoskrnl need not appear as a UEFI loaded-image handle. The local generic
KernelErrataPatcherLib captures the EBS caller return address, walks backward
for a PE header, and performs semester signatures. That broad untyped search
does not meet the identity requirements of this experiment and should not be
reused as proof that an address is the selected kernel.

A locator-only diagnostic can first inspect the actual EBS memory map,
restrict candidates to bounded loader allocations, and require a unique
matching PE image with checked header/section bounds, exact RSDS and all
related path guards, plus the full relocation-normalized template. No MMIO,
arbitrary physical sweep, best-looking match, or fault-PC-minus-preferred-base
inference is warranted. A valid PE can cross descriptor boundaries; every byte
read must be covered by appropriate adjacent map ranges. If the complete
image is not discoverable safely, the locator must report failure and leave
all candidates unchanged. Template/RSDS checks establish the exact local
patch target; they do not cryptographically authenticate the complete loaded
image in isolation.

Kernel HAL startup follows loader handoff and successful EBS. Consequently a
correctly qualified EBS callback is before the builder copies the template;
actual loaded allocation visibility on this phone is not yet captured.
Read-only locator qualification precedes enabling any source write. A future
writer must handle memory attributes, avoid memory-map-key-changing
allocations at the final callback, publish the changed source instructions
through the architecture's cache-maintenance operations, and record the exact
old/new bytes and selected physical base. Failed EBS/retry behavior must be
idempotent and explicitly logged. Reset removes a RAM-only source patch.

The direct global-builder caller is A8D7B8 in PAGELK, flags60000020, and the
builder itself is in `.text`. There is no existing discardable INIT caller
in that direct chain. A destination-only postcopy hook could eventually keep
source text intact, but requires a separately established executable wrapper,
address mapping and restoration/lifetime design. No such safe code cave or
wrapper is claimed here.

## Signing and runtime integrity

An on-disk kernel modification invalidates its existing signed-file content.
Microsoft documents that Trusted Boot verifies the kernel before loading it;
test signing is not evidence that an edited Microsoft kernel is accepted.
This audit produces no on-disk kernel, modified boot loader, signature patch,
or integrity bypass. See [Microsoft Trusted Boot](https://learn.microsoft.com/windows/security/operating-system-security/system-security/trusted-boot).

An EBS RAM modification occurs after the ordinary file load but does not
prove that all later validation has completed. HVCI can enforce validated
executable content and write/execute separation; its actual state here is not
established by EL1 observation or BCD alone. See [Microsoft memory integrity
enablement](https://learn.microsoft.com/en-ca/windows-hardware/design/device-experiences/oem-hvci-enablement).

No exact-build proof was established that ARM64 kernel integrity/PatchGuard
will tolerate modified source `.text` or that an early checksum has already
covered it. Disabling HVCI or patching validation is outside this experiment.
A bounded early AP-start observation would therefore prove only that startup
passed the previous failure, even if it reaches several processors. It would
not prove a stable, supported Windows installation or freedom from a later
integrity stop.

## Produced fixture and verification

`diagnostic/degas-windows-context-bridge/bridge_fixture.py` validates the
original saved SHA256 and exact loaded PE/template/path guards, generates the
14 instruction words for a supplied base, and returns a separate host
bytearray. It has no device, file patching, runtime memory discovery, or
firmware integration function. `export_header.py` emits
`NativeContextFixture.h` containing reviewed identity/template/prefix bytes
for separately reviewed integration. Its fixed MOVZ/MOVK words deliberately
contain no usable mailbox base until the caller inserts the validated value.

`test_bridge.py` passes10tests, with49 AP instruction cases, two actual global
builder executions and three actual native caller/SMC-path executions.
`host-results.json` contains the individual records. Coverage includes all
eight MPIDRs, native correct/zero contexts, bridge zero/correct/arbitrary
incoming contexts, unchanged original tail, DAIF precondition, dynamic
MOVZ/MOVK bases,64KiB crossing, the upper4GiB allocation boundary, ASLR
normalization with separate physical/virtual bases, and rejection of
changed/truncated identity bytes.

Unicorn fixes MPIDR_EL1 read-only at80000000, so the single hardware MRS input
is explicitly modeled as the captured-style raw81000000|(CPU<<8); recovery
arithmetic executes as actual AArch64 instructions. Native zero-context
execution faults at the actual200100 load, while bridge execution reaches the
same TTBR/TPIDR/PCR/stack state as an original valid-context execution. Tests
execute the unchanged body through200180 and stop immediately before the
200184 SCTLR write that would enable Windows mappings. External memory
helpers, CFG validation and SMC return are separately labeled models. This
does not emulate GZ, GIC interrupts, hardware cache visibility, MMU enable,
Windows scheduling, HVCI or kernel integrity. Original source SHA256 is checked
again after every suite run and remains unchanged.

The next permitted implementation work is a separate opt-in UEFI locator and
reviewable RAM-only integration. Hardware boot, Windows experimental-copy
deployment, or any reboot remains a later user-controlled step. No early
firmware repair is needed for this experiment, and no early firmware write is
authorized by this report.
